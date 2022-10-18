#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

// prototypes
static void defineNativeFunction(const char* name, NativeFn function);
inline CallFrame* currentCallFrame();

VM vm;

/// <summary>
/// Reset stack pointer.
/// </summary>
/// <param name="vm"></param>
static void resetStack()
{
	vm.stack.count = 0;
	vm.frameCount = 0;
	vm.openUpvalues = NULL;
	// no need to actually de-allocate anything
}

void initStack(VM* vm)
{
	initValueArray(&vm->stack);
	vm->stack.capacity = STACK_DEFAULT;
	vm->stack.values = GROW_ARRAY(Value, vm->stack.values, 0, STACK_DEFAULT);
	resetStack();
}

void freeVM(VM* vm)
{
	// free stack
	freeValueArray(&vm->stack);

	// force GC
	vm->initString = NULL;
	freeObjects(vm->objects);

	freeTable(&vm->strings);
	freeTable(&vm->globals);

	// reset fields
	initVM(vm);
}

void initNativeFunctions()
{
	// init native functions
	defineNativeFunction("clock", clockNative);
}

void initVM(VM* vm)
{
	vm->exitCode = -1; // interrupted
	vm->objects = NULL;
	vm->bytesAllocated = 0;
	vm->nextGC = 1024 * 1024;

	// init gray stack
	vm->grayCount = 0;
	vm->grayCapacity = 0;
	vm->grayStack = NULL;

	initValueArray(&vm->stack);
	initTable(&vm->strings);
	initTable(&vm->globals);

	// constant strings
	vm->initString = NULL; // zero-memory in case copyString runs GC
	vm->initString = copyString(INIT_STRING, INIT_STRING_LENGTH);
}

/// <summary>
/// Log error and reset state.
/// </summary>
static void runtimeError(const char* format, ...)
{
	// log message
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	// stack trace
	for (int32_t i = vm.frameCount - 1; i >= 0; --i)
	{
		CallFrame* frame = currentCallFrame();
		ObjectFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1; // prev instruction is culprit
		uint32_t line = function->chunk.lines[instruction];
		fprintf(stderr, "[line %d] in script\n", line);
		if (function->name == NULL)
			fprintf(stderr, "script\n");
		else
			fprintf(stderr, "%s()\n", function->name->chars);
	}

	// reset state
	resetStack();
}

static void defineNativeFunction(const char* name, NativeFn function)
{
	// push and pop to account for GC occurring due to allocations
	push(OBJECT_VAL(copyString(name, (uint32_t)strlen(name))));
	push(OBJECT_VAL(newNativeFunction(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack.values[0]), vm.stack.values[1]);
	pop();
	pop();
}

void push(Value value)
{
	writeValueArray(&vm.stack, value);
}

Value pop()
{
	return vm.stack.values[--vm.stack.count];
}

/// <summary>
/// Points to the top (uninitialized) of the stack.
/// </summary>
inline Value* stackTop()
{
	return &vm.stack.values[vm.stack.count];
}

inline CallFrame* currentCallFrame()
{
	return &vm.callStack[vm.frameCount - 1];
}

inline Value peek(uint32_t distance)
{
	return vm.stack.values[vm.stack.count - 1 - distance];
}

/// <returns>False if a runtime error ocurred.</returns>
static bool call(ObjectClosure* closure, uint8_t argCount)
{
	ObjectFunction* function = closure->function; // fetch once
	// validate number of arguments against parameters
	if (argCount != function->arity)
	{
		runtimeError("Expected %d arguments but got %d.",
			function->arity, argCount);
		return false;
	}

	// guard against stack overflow
	if (vm.frameCount == FRAMES_MAX)
	{
		runtimeError("Stack overflow.");
		return false;
	}

	// setup call frame
	CallFrame* frame = &vm.callStack[vm.frameCount++];
	frame->closure = closure;
	frame->ip = function->chunk.code;
	// slots are function name and parameters
	uint32_t stackSize = vm.stack.count - argCount - 1;
	frame->slots = &vm.stack.values[stackSize];
	frame->stackOffset = stackSize; // 
	return true;
}

bool callValue(Value callee, uint8_t argCount)
{
	if (IS_OBJECT(callee))
	{
		switch (OBJECT_TYPE(callee))
		{
			case OBJECT_BOUND_METHOD:
			{
				ObjectBoundMethod* boundMethod = AS_BOUND_METHOD(callee);
				stackTop()[-argCount - 1] = boundMethod->receiver; // put 'this' in slot[0]
				return call(boundMethod->method, argCount);
			}
			case OBJECT_CLASS: // class constructor
			{
				ObjectClass* _class = AS_CLASS(callee);
				stackTop()[-argCount - 1] = OBJECT_VAL(newInstance(_class));

				// initializer
				Value initializer;
				if (tableGet(&_class->methods, vm.initString, &initializer))
				{
					return call(AS_CLOSURE(initializer), argCount);
				}
				else if (argCount != 0) // disallow passing args to a class with no initializer
				{
					runtimeError("Expected 0 arguments (no initializer defined) but got %d.",
						argCount);
					return false;
				}

				return true;
			}
			case OBJECT_CLOSURE: return call(AS_CLOSURE(callee), argCount);
			case OBJECT_NATIVE:
			{
				NativeFn native = AS_NATIVE(callee);
				Value* argv = &vm.stack.values[vm.stack.count - argCount];
				Value result = native(argCount, argv);
				vm.stack.count -= argCount + 1; // deallocate args off stack
				push(result); // return value
				return true;
			}
			default: break; // error
		}
	}
	runtimeError("Can only call functions and classes.");
	return false;
}

static bool invokeFromClass(ObjectClass* klass, ObjectString* name, uint8_t argCount)
{
	Value method;

	if (!tableGet(&klass->methods, name, &method))
	{
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	// items are already on the stack where they belong
	return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjectString* name, uint8_t argCount)
{
	Value receiver = peek(argCount); // instance is item preceding args

	// validate proper use
	if (!IS_INSTANCE(receiver))
	{
		runtimeError("Only instances have methods.");
		return false;
	}

	ObjectInstance* instance = AS_INSTANCE(receiver);

	// handle function stored in a field being called correctly
	Value value;
	if (tableGet(&instance->fields, name, &value))
	{
		stackTop()[-argCount - 1] = value; // replace instance with callee on stack
		return callValue(value, argCount);
	}

	// invoke method
	return invokeFromClass(instance->_class, name, argCount);
}

/// <summary>
/// Binds a method to an Instance and replaces it on the stack.
/// </summary>
static bool bindMethod(ObjectClass* klass, ObjectString* name)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method))
	{
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	// bind the method
	ObjectBoundMethod* boundMethod = newBoundMethod(
		peek(0), AS_CLOSURE(method));
	pop(); // owning class
	push(OBJECT_VAL(boundMethod));
	return true;
}

static ObjectUpvalue* captureUpvalue(Value* local)
{
	ObjectUpvalue* prevUpvalue = NULL;
	// start at upvalue closest to stack
	ObjectUpvalue* upvalue = vm.openUpvalues; // return value

	// sorted linked-list traversal
	while (upvalue != NULL && upvalue->location > local)
	{
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	// did we find it or should we make a new one?
	if (upvalue != NULL && upvalue->location == local)
		return upvalue; // re-use existing variable
	
	// create a new one
	ObjectUpvalue* createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	// add to linked-list
	if (prevUpvalue == NULL) // no head (first)
		vm.openUpvalues = createdUpvalue; // head
	else
		prevUpvalue->next = createdUpvalue; // tail

	return createdUpvalue;
}

static void closeUpvalues(Value* last)
{
	while (vm.openUpvalues != NULL
		&& vm. openUpvalues->location >= last)
	{
		ObjectUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location; // move the value to heap
		upvalue->location = &upvalue->closed; // point to location on heap
		vm.openUpvalues = upvalue->next;
	}
}

static void defineMethod(ObjectString* name)
{
	Value method = peek(0);
	ObjectClass* klass = AS_CLASS(peek(1));
	tableSet(&klass->methods, name, method);
	pop(); // pop method, leave class
}

static bool isFalsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

/// <summary>
/// Concatenates two strings together.
/// </summary>
static void concatenate()
{
	ObjectString* b = AS_STRING(peek(0)); // keep on stack for gc
	ObjectString* a = AS_STRING(peek(1));

	uint32_t length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1); // \0
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0'; // null-terminated

	ObjectString* result = takeString(chars, length);
	pop(); pop();
	push(OBJECT_VAL(result));
}

static InterpretResult run()
{
	CallFrame* frame = currentCallFrame();

#define READ_BYTE() (*(frame->ip++))
#define READ_16() \
	(frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_24() \
	(frame->ip += 3, (uint32_t)((frame->ip[-3] << 16) | (frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_32() \
	(frame->ip += 4, (uint32_t)((frame->ip[-4] << 24) | (frame->ip[-3] << 16) | (frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (frame->closure->function->chunk.constants.values[READ_24()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() AS_STRING(READ_CONSTANT_LONG())
#define BINARY_OP(valueType, op) do \
{ \
	if (!IS_NUMBER(peek(0))) \
	{ \
		runtimeError("Right-hand operand must be a number."); \
		return INTERPRET_RUNTIME_ERROR; \
	} \
	else if (!IS_NUMBER(peek(1))) \
	{ \
		runtimeError("Left-hand operand must be a number."); \
		return INTERPRET_RUNTIME_ERROR; \
	} \
	double b = AS_NUMBER(pop()); \
	double a = AS_NUMBER(pop()); \
	push(valueType(a op b)); \
} while (false) \

	// work
	while (1)
	{
#ifdef DEBUG_TRACE_EXECUTION
		printf("        ");

		//for (Value* slot = vm->stack.values; slot < vm->stack.count; slot++)
		for (uint32_t i = 0; i < vm.stack.count; ++i)
		{
			Value value = vm.stack.values[i];
			printf("[ ");
			printValue(value);
			printf(" ]");
		}

		disassembleInstruction(&(frame->closure->function->chunk),
			(uint32_t)(frame->ip - frame->closure->function->chunk.code)); //offset
#endif

		// decode instruction
		// consider �direct threaded code�, �jump table�, and �computed goto�.
		OpCode operation = READ_BYTE();
		switch (operation)
		{
			// constants
			case OP_CONSTANT: push(READ_CONSTANT()); break;
			case OP_CONSTANT_LONG: push(READ_CONSTANT_LONG()); break;// function works, macro doesn't
			case OP_CONSTANT_ZERO:
				push(frame->closure->function->chunk.constants.values[0]);
				break;

			// literals
			case OP_ZERO: push(NUMBER_VAL(0)); break;
			case OP_ONE: push(NUMBER_VAL(1)); break;
			case OP_NEG_ONE: push(NUMBER_VAL(-1)); break;
			case OP_NIL: push(NIL_VAL); break;
			case OP_TRUE: push(BOOL_VAL(true)); break;
			case OP_FALSE: push(BOOL_VAL(false)); break;
			case OP_POP: pop(); break; // discard 
			case OP_POPN: vm.stack.count -= READ_BYTE(); break;

			// variable accessors
			case OP_GET_LOCAL:
			{
				uint8_t slot = READ_BYTE();
				push(frame->slots[slot]);
				break;
			}
			case OP_SET_LOCAL:
			{
				// leave val on stack to support 'a = b = c = 10;'
				uint8_t slot = READ_BYTE();
				frame->slots[slot] = peek(0);
				break;
			}
			case OP_DEFINE_GLOBAL: // consider LONG constants
			{
				ObjectString* name = READ_STRING();
				tableSet(&vm.globals, name, peek(0)); // can easily redefine globals
				pop(); // discard after to be considerate of GC
				break;
			}
			case OP_GET_GLOBAL:
			{
				ObjectString* name = READ_STRING();
				Value value;
				if (!tableGet(&vm.globals, name, &value))
				{
					runtimeError("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				push(value);
				break;
			}
			case OP_SET_GLOBAL:
			{
				ObjectString* name = READ_STRING();

				// undefined?
				if (tableSet(&vm.globals, name, peek(0)))
				{
					tableDelete(&vm.globals, name); // undo mistake
					runtimeError("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_GET_SUPER:
			{
				ObjectString* name = READ_STRING(); // member of super
				ObjectClass* superclass = AS_CLASS(pop());

				// 'super' always resolves to a method, since fields are not inherited
				if (!bindMethod(superclass, name))
					return INTERPRET_RUNTIME_ERROR;

				break;
			}
			case OP_GET_UPVALUE:
			{
				uint8_t slot = READ_BYTE();
				push(*frame->closure->upvalues[slot]->location);
				break;
			}
			case OP_SET_UPVALUE:
			{
				// don't pop because assignment is an expression
				uint8_t slot = READ_BYTE();
				*frame->closure->upvalues[slot]->location = peek(0);
				break;
			}

			// properties
			case OP_GET_PROPERTY:
			case OP_GET_PROPERTY_LONG:
			{
				// guard against not accessing an instance
				if (!IS_INSTANCE(peek(0)))
				{
					runtimeError("Only instances have properties.");
					return INTERPRET_RUNTIME_ERROR;
				}

				bool isLong = operation == OP_GET_PROPERTY_LONG;
				ObjectInstance* instance = AS_INSTANCE(peek(0)); // cast
				ObjectString* name = isLong ? READ_STRING_LONG() : READ_STRING();

				// search fields
				Value value;
				if (tableGet(&instance->fields, name, &value))
				{
					pop(); // Instance
					push(value); //
					break;
				}

				// search methods
				if (!bindMethod(instance->_class, name))
					return INTERPRET_RUNTIME_ERROR;
				break;
			}
			case OP_SET_PROPERTY:
			case OP_SET_PROPERTY_LONG:
			{
				// guard against not accessing an instance
				if (!IS_INSTANCE(peek(1)))
				{
					runtimeError("Only instances have fields.");
					return INTERPRET_RUNTIME_ERROR;
				}

				bool isLong = operation == OP_SET_PROPERTY_LONG;
				ObjectInstance* instance = AS_INSTANCE(peek(1));
				ObjectString* name = isLong ? READ_STRING_LONG() : READ_STRING();
				tableSet(&instance->fields, name, peek(0));
				Value value = pop(); // pop the result of the get
				pop(); // pop the instance
				push(value); // push the assigned value back to allow chaining
				break;
			}

			// boolean
			case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
			case OP_EQUAL:
			{
				Value b = pop();
				Value a = pop();
				push(BOOL_VAL(valuesEqual(a, b)));
				break;
			}
			case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
			case OP_LESS: BINARY_OP(BOOL_VAL, < ); break;

			// arithmetic
			case OP_ADD: // BINARY_OP(NUMBER_VAL, +); break;
			{
				if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
				{
					concatenate();
				}
				else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1)))
				{
					double b = AS_NUMBER(pop());
					double a = AS_NUMBER(pop());
					push(NUMBER_VAL(a + b));
				}
				else
				{
					runtimeError("Operands must be two numbers or two strings.");
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
			case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
			case OP_DIVIDE: // BINARY_OP(NUMBER_VAL, /); break;
			{
				if (!IS_NUMBER(peek(0)))
				{
					runtimeError("Right-hand operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				else if (!IS_NUMBER(peek(1)))
				{
					runtimeError("Left-hand operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}

				double b = AS_NUMBER(pop());
				if (b == 0) // div 0
				{
					runtimeError("Divide by zero.");
					return INTERPRET_RUNTIME_ERROR;
				}
				double a = AS_NUMBER(pop());
				double quotient = a / b;
				push(NUMBER_VAL(quotient));
				break;
			}
			case OP_NEGATE:
			{
				// type check
				if (!IS_NUMBER(peek(0)))
				{
					runtimeError("Operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}

				uint32_t top = vm.stack.count - 1;
				vm.stack.values[top] = NUMBER_VAL(-AS_NUMBER(vm.stack.values[top])); // challenge: avoid push/pop for unary op
				break;
			}
			case OP_JUMP_IF_FALSE:
			{
				uint16_t offset = READ_16(); // always consume args
				if (isFalsey(peek(0)))
					frame->ip += offset;
				break;
			}
			case OP_JUMP:
			{
				uint16_t offset = READ_16();
				frame->ip += offset;
				break;
			}
			case OP_LOOP:
			{
				uint16_t offset = READ_16();
				frame->ip -= offset;
				break;
			}
			case OP_CALL:
			{
				uint8_t argCount = READ_BYTE();
				if (!callValue(peek(argCount), argCount))
					return INTERPRET_RUNTIME_ERROR;
				frame = currentCallFrame(); // reset base pointer
				break;
			}
			case OP_INVOKE:
			{
				// get operands
				ObjectString* method = READ_STRING();
				uint8_t argCount = READ_BYTE();

				// invoke method with args
				if (!invoke(method, argCount))
					return INTERPRET_RUNTIME_ERROR;

				// reset base pointer
				frame = currentCallFrame();

				break;
			}
			case OP_SUPER_INVOKE:
			{
				// get operands
				ObjectString* method = READ_STRING();
				uint8_t argCount = READ_BYTE();
				ObjectClass* superclass = AS_CLASS(pop());

				if (!invokeFromClass(superclass, method, argCount))
					return INTERPRET_RUNTIME_ERROR;

				frame = currentCallFrame(); // reset base pointer
				break;
			}
			case OP_CLOSURE:
			case OP_CLOSURE_LONG:
			{
				bool isLong = operation == OP_CLOSURE_LONG;
				ObjectFunction* function = isLong
					? AS_FUNCTION(READ_CONSTANT_LONG())
					: AS_FUNCTION(READ_CONSTANT());
				ObjectClosure* closure = newClosure(function);
				push(OBJECT_VAL(closure));

				// handle upvalues
				for (uint32_t i = 0; i < closure->upvalueCount; ++i)
				{
					uint8_t isLocal = READ_BYTE();
					uint8_t index = READ_BYTE();
					if (isLocal)
						closure->upvalues[i] = captureUpvalue(frame->slots + index);
					else // is already captured
						closure->upvalues[i] = frame->closure->upvalues[index];
				}
				break;
			}
			case OP_PRINT:
			{
				printValue(pop());
				printf("\n");
				break;
			}
			case OP_CLOSE_UPVALUE:
			{
				closeUpvalues(&vm.stack.values[vm.stack.count - 1]);
				pop();
				break;
			}
			case OP_RETURN:
			{
				Value result = pop(); // the return value
				closeUpvalues(frame->slots); // close function's params and locals
				uint32_t count = --vm.frameCount; // pop callstack

				// is program complete
				if (count == 0)
				{
					pop(); // pop <script>
					if (IS_BOOL(result))
					{
						// 'true' indicates 'success' and 'false' indicates 'failure'.
						vm.exitCode = AS_BOOL(result) ? 0 : -1;
					}
					else if (IS_NUMBER(result))
					{
						// assumes cast will work
						vm.exitCode = (uint64_t)AS_NUMBER(result);
					}
					else if (IS_NIL(result))
					{
						// normal exit (possibly an early-exit)
						vm.exitCode = 0;
					}
					// TODO - return a string through stdout?
					else
					{
						runtimeError("Can only return number, nil, or bool.");
						return INTERPRET_RUNTIME_ERROR;
					}
					return INTERPRET_OK; // exit
				}

				// deallocate locals, args, function name
				//uint32_t locals = (&vm.stack.values[vm.stack.count] - frame->slots);
				vm.stack.count -= vm.stack.count - frame->stackOffset;
				//vm.stack.count -= locals;

				// return statement
				push(result); // set return value
				frame = &vm.callStack[count - 1]; // restore previous base pointer
				break;
			}

			// classes
			case OP_CLASS:
				push(OBJECT_VAL(newClass(READ_STRING()))); break;
			case OP_INHERIT:
			{
				Value superclass = peek(1);

				// verify self-respecting user code
				if (!IS_CLASS(superclass))
				{
					runtimeError("Can only inherit from a class.");
					return INTERPRET_RUNTIME_ERROR;
				}

				ObjectClass* subclass = AS_CLASS(peek(0));

				// copy-down inheritance
				copyTable(&AS_CLASS(superclass)->methods,
					&subclass->methods);

				pop(); // subclass
				break;
			}
			case OP_METHOD: defineMethod(READ_STRING()); break;
			case OP_METHOD_LONG: defineMethod(READ_STRING_LONG()); break;

			default:
			{
				runtimeError("Opcode not accounted for!");
				return INTERPRET_RUNTIME_ERROR;
			}
		}
	}

#undef BINARY_OP
#undef READ_STRING
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef READ_LONG
#undef READ_SHORT
#undef READ_BYTE
}

InterpretResult interpret(const char* source)
{
	ObjectFunction* function = compile(source);

	// handle compilation error
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	push(OBJECT_VAL(function)); // push for GC
	ObjectClosure* closure = newClosure(function);
	pop();
	push(OBJECT_VAL(closure));
	call(closure, 0); // main()

	return run();
}
