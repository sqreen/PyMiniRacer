#undef RUBY_COMPILE

// https://docs.python.org/2/library/ctypes.html
#define PYTHON_COMPILE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RUBY_COMPILE
#include <ruby.h>
#include <ruby/thread.h>
#endif
#include <include/v8.h>
#include <include/libplatform/libplatform.h>
#ifdef RUBY_COMPILE
#include <ruby/encoding.h>
#endif
#include <pthread.h>
#include <unistd.h>

/* Python version need to fix:
 * - clear memory of returned objects
 * - provide a way to compile on, execute many times
 */

#ifdef PYTHON_COMPILE
static void * xalloc(size_t x) {
    void *tmp = malloc(x);
    if (tmp == NULL) {
        abort();
    }
    return tmp;
}
#define ALLOC(x) ((x *) xalloc(sizeof(x)));

#define TYPE(x) x->type
// #define PSTRING_PTR(x) (char *) ((x->type == type_str) ? x->value: (void *) abort());
#define PSTRING_PTR(x) ((char *) x->value)

enum PythonTypes {
    type_null = 1,
    type_bool = 2,
    type_integer = 3,
    type_float = 4, // unused
    type_double = 5,
    type_str = 6, // unused
    type_str_utf8 = 7,
    type_array = 8,
    type_hash = 9,
    type_function = 10,
    type_exception = 11,
    type_invalid = 12,
};

#define T_STRING type_str_utf8

/* This is a generic store for arbitrary JSON like values.
 * Non scalar values are:
 *  - Strings: pointer to a string
 *  - Arrays: contiguous map of pointers to PythonValue
 *  - Hash: contiguous map of pair of pointers to PythonTypes (first is key,
 *          second is value)
 */
typedef struct {
    void *value;
    enum PythonTypes type;
    size_t len;
} PythonValue;


void PythonValueFree(PythonValue *v) {
    size_t i=0;
    if (!v) {
        return;
    }
    switch(v->type) {
        case type_str:
        case type_str_utf8:
            free(v->value);
            break;
        case type_array:
            for(i=0; i < v->len; i++) {
                PythonValue *w = ((PythonValue **) v->value)[i];
                PythonValueFree(w);
            }
            break;
        case type_hash:
            for(i=0; i < v->len; i++) {
                PythonValue *k = ((PythonValue **) v->value)[i*2];
                PythonValue *w = ((PythonValue **) v->value)[i*2+1];
                PythonValueFree(k);
                PythonValueFree(w);
            }
            break;
        default:
            // the other types are scalar values
            break;
    }
    free(v);
}

#endif

using namespace v8;

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

typedef struct {
    Isolate* isolate;
    Persistent<Context>* context;
    ArrayBufferAllocator* allocator;
    bool interrupted;
} ContextInfo;

typedef struct {
    bool parsed;
    bool executed;
    bool terminated;
    Persistent<Value>* value;
    Persistent<Value>* message;
    Persistent<Value>* backtrace;
} EvalResult;

typedef struct {
    ContextInfo* context_info;
    Local<String>* eval;
    useconds_t timeout;
    EvalResult* result;
} EvalParams;

#ifdef RUBY_COMPILE
static VALUE rb_eScriptTerminatedError;
static VALUE rb_eParseError;
static VALUE rb_eScriptRuntimeError;
static VALUE rb_cJavaScriptFunction;
#endif

static Platform* current_platform = NULL;

static void init_v8() {
    if (current_platform == NULL) {
	V8::InitializeICU();
	current_platform = platform::CreateDefaultPlatform();
	V8::InitializePlatform(current_platform);
	V8::Initialize();
    }
}

void* breaker(void *d) {
  EvalParams* data = (EvalParams*)d;
  usleep(data->timeout*1000);
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  V8::TerminateExecution(data->context_info->isolate);
  return NULL;
}

void*
nogvl_context_eval(void* arg) {
    EvalParams* eval_params = (EvalParams*)arg;
    EvalResult* result = eval_params->result;
    Isolate* isolate = eval_params->context_info->isolate;
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);

    TryCatch trycatch(isolate);

    Local<Context> context = eval_params->context_info->context->Get(isolate);

    Context::Scope context_scope(context);

    MaybeLocal<Script> parsed_script = Script::Compile(context, *eval_params->eval);
    result->parsed = !parsed_script.IsEmpty();
    result->executed = false;
    result->terminated = false;
    result->value = NULL;

    if (!result->parsed) {
	result->message = new Persistent<Value>();
	result->message->Reset(isolate, trycatch.Exception());
    } else {

	pthread_t breaker_thread;

	if (eval_params->timeout > 0) {
	   pthread_create(&breaker_thread, NULL, breaker, (void*)eval_params);
	}

	MaybeLocal<Value> maybe_value = parsed_script.ToLocalChecked()->Run(context);

	if (eval_params->timeout > 0) {
	    pthread_cancel(breaker_thread);
	    pthread_join(breaker_thread, NULL);
	}

	result->executed = !maybe_value.IsEmpty();

	if (!result->executed) {
	    if (trycatch.HasCaught()) {
		if (!trycatch.Exception()->IsNull()) {
		    result->message = new Persistent<Value>();
		    result->message->Reset(isolate, trycatch.Exception()->ToString());
		} else if(trycatch.HasTerminated()) {
		    result->terminated = true;
		    result->message = new Persistent<Value>();
		    Local<String> tmp = String::NewFromUtf8(isolate, "JavaScript was terminated (either by timeout or explicitly)");
		    result->message->Reset(isolate, tmp);
		}

		if (!trycatch.StackTrace().IsEmpty()) {
		    result->backtrace = new Persistent<Value>();
		    result->backtrace->Reset(isolate, trycatch.StackTrace()->ToString());
		}
	    }
	} else {
	    Persistent<Value>* persistent = new Persistent<Value>();
	    persistent->Reset(isolate, maybe_value.ToLocalChecked());
	    result->value = persistent;
	}
    }

    return NULL;
}

#ifdef RUBY_COMPILE
static VALUE convert_v8_to_ruby(Isolate* isolate, Handle<Value> &value) {

    HandleScope scope(isolate);

    if (value->IsNull() || value->IsUndefined()){
	return Qnil;
    }

    if (value->IsInt32()) {
     return INT2FIX(value->Int32Value());
    }

    if (value->IsNumber()) {
      return rb_float_new(value->NumberValue());
    }

    if (value->IsTrue()) {
      return Qtrue;
    }

    if (value->IsFalse()) {
      return Qfalse;
    }

    if (value->IsArray()) {
      VALUE rb_array = rb_ary_new();
      Local<Array> arr = Local<Array>::Cast(value);
      for(uint32_t i=0; i < arr->Length(); i++) {
	  Local<Value> element = arr->Get(i);
	  VALUE rb_elem = convert_v8_to_ruby(isolate, element);
          rb_ary_push(rb_array, rb_elem);
      }
      return rb_array;
    }

    if (value->IsFunction()){
	return rb_funcall(rb_cJavaScriptFunction, rb_intern("new"), 0);
    }

    if (value->IsObject()) {
        VALUE rb_hash = rb_hash_new();
        Local<Context> context = Context::New(isolate);
        Local<Object> object = value->ToObject();
        MaybeLocal<Array> maybe_props = object->GetOwnPropertyNames(context);
        if (!maybe_props.IsEmpty()) {
            Local<Array> props = maybe_props.ToLocalChecked();
            for(uint32_t i=0; i < props->Length(); i++) {
                Local<Value> key = props->Get(i);
                VALUE rb_key = convert_v8_to_ruby(isolate, key);
                Local<Value> value = object->Get(key);
                VALUE rb_value = convert_v8_to_ruby(isolate, value);
                rb_hash_aset(rb_hash, rb_key, rb_value);
            }
        }
        return rb_hash;
    }

    Local<String> rstr = value->ToString();
    return rb_enc_str_new(*v8::String::Utf8Value(rstr), rstr->Utf8Length(), rb_enc_find("utf-8"));
}
#endif

#ifdef PYTHON_COMPILE
PythonValue *convert_v8_to_python(Isolate* isolate, Handle<Value> &value) {
    HandleScope scope(isolate);

    PythonValue *res = ALLOC(PythonValue);
    res->len = 0;
    res->value = NULL;
    res->type = type_invalid;

    if (value->IsNull() || value->IsUndefined()){
        res->type = type_null;
        res->value = NULL;
        return res;
    }

    if (value->IsInt32()) {
        res->type = type_integer;
        uint32_t val = value->Int32Value();
        *((uint32_t *) &res->value) = val;
        return res;
    }

    // ECMA-262, 4.3.20
    // http://www.ecma-international.org/ecma-262/5.1/#sec-4.3.19
    if (value->IsNumber()) {
        res->type = type_double;
        double val = value->NumberValue();
        *((double *) &res->value) = val;
        return res;
    }

    if (value->IsTrue()) {
        res->type = type_bool;
        res->value = (void *) 1;
      return res;
    }

    if (value->IsFalse()) {
        res->type = type_bool;
        res->value = (void *) 0;
    }

    if (value->IsArray()) {
        Local<Array> arr = Local<Array>::Cast(value);
        size_t len = arr->Length();
        PythonValue **ary = (PythonValue **) malloc(sizeof(PythonValue) * len);
        if (!ary) {
            free(res);
            return NULL;
        }

        for(uint32_t i=0; i < arr->Length(); i++) {
            Local<Value> element = arr->Get(i);
            PythonValue *py_value = convert_v8_to_python(isolate, element);
            if (py_value == NULL) {
                free(res);
                free(ary);
                return NULL;
            }
            ary[i] = py_value;
        }
        res->type = type_array;
        res->len = len;
        res->value = (void *) ary;
        return res;
    }

    // FIXME: function not supported yet
    if (value->IsFunction()){
        res->type = type_function;
        res->value = NULL;
        res->len = 0;
        return res;
        // return rb_funcall(rb_cJavaScriptFunction, rb_intern("new"), 0);
    }

    if (value->IsObject()) {
        res->type = type_hash;

        Local<Context> context = Context::New(isolate);
        Local<Object> object = value->ToObject();
        MaybeLocal<Array> maybe_props = object->GetOwnPropertyNames(context);
        if (!maybe_props.IsEmpty()) {
            Local<Array> props = maybe_props.ToLocalChecked();
            uint32_t hash_len = props->Length();

            res->len = hash_len;
            // FIXME overflow
            if (hash_len > 0) {
                res->value = malloc(sizeof(void *) * hash_len * 2);
                if (!res->value) {
                    free(res);
                    return NULL;
                }
            } else {
                res->value = NULL;
            }
            for(uint32_t i=0; i < hash_len; i++) {
                Local<Value> key = props->Get(i);
                PythonValue *py_key = convert_v8_to_python(isolate, key);
                Local<Value> value = object->Get(key);
                PythonValue *py_value = convert_v8_to_python(isolate, value);
                // r_hash_aset(rb_hash, rb_key, rb_value);
                ((PythonValue **) res->value)[i*2]   = py_key;
                ((PythonValue **) res->value)[i*2+1] = py_value;
            }
        } else {
            // empty hash
            res->len = 0;
            res->value = NULL;
        }
        return res;
    }

    Local<String> rstr = value->ToString();

    res->type = type_str_utf8;
    res->len = (size_t) rstr->Utf8Length();
    res->value = (void *) strdup((char *) *v8::String::Utf8Value(rstr));
    return res;
}
#if 0
static Handle<Value> convert_ruby_to_v8(Isolate* isolate, VALUE value) {
    EscapableHandleScope scope(isolate);

    Local<Array> array;
    Local<Object> object;
    VALUE hash_as_array;
    VALUE pair;
    int length,i;

    switch (TYPE(value)) {
    case T_FIXNUM:
	return scope.Escape(Integer::New(isolate, NUM2INT(value)));
    case T_FLOAT:
	return scope.Escape(Number::New(isolate, NUM2DBL(value)));
    case T_STRING:
	return scope.Escape(String::NewFromUtf8(isolate, RSTRING_PTR(value), NewStringType::kNormal, (int)RSTRING_LEN(value)).ToLocalChecked());
    case T_NIL:
	return scope.Escape(Null(isolate));
    case T_TRUE:
	return scope.Escape(True(isolate));
    case T_FALSE:
	return scope.Escape(False(isolate));
    case T_ARRAY:
	length = RARRAY_LEN(value);
	array = Array::New(isolate, length);
	for(i=0; i<length; i++) {
	    array->Set(i, convert_ruby_to_v8(isolate, rb_ary_entry(value, i)));
	}
	return scope.Escape(array);
    case T_HASH:
	object = Object::New(isolate);
	hash_as_array = rb_funcall(value, rb_intern("to_a"), 0);
	length = RARRAY_LEN(hash_as_array);
	for(i=0; i<length; i++) {
	    pair = rb_ary_entry(hash_as_array, i);
	    object->Set(convert_ruby_to_v8(isolate, rb_ary_entry(pair, 0)),
			convert_ruby_to_v8(isolate, rb_ary_entry(pair, 1)));
	}
	return scope.Escape(object);
    case T_SYMBOL:
	value = rb_funcall(value, rb_intern("to_s"), 0);
	return scope.Escape(String::NewFromUtf8(isolate, RSTRING_PTR(value), NewStringType::kNormal, (int)RSTRING_LEN(value)).ToLocalChecked());
    case T_DATA:
    case T_OBJECT:
    case T_CLASS:
    case T_ICLASS:
    case T_MODULE:
    case T_REGEXP:
    case T_MATCH:
    case T_STRUCT:
    case T_BIGNUM:
    case T_FILE:
    case T_UNDEF:
    case T_NODE:
    default:
      return scope.Escape(String::NewFromUtf8(isolate, "Undefined Conversion"));
    }

}
#endif
#endif

static void unblock_eval(void *ptr) {
    EvalParams* eval = (EvalParams*)ptr;
    eval->context_info->interrupted = true;
}


#ifdef RUBY_COMPILE
static VALUE rb_context_eval_unsafe(VALUE self, VALUE str) {

    EvalParams eval_params;
    EvalResult eval_result;
    ContextInfo* context_info;
    VALUE result;

    VALUE message = Qnil;
    VALUE backtrace = Qnil;

    Data_Get_Struct(self, ContextInfo, context_info);

    {
	Locker lock(context_info->isolate);
	Isolate::Scope isolate_scope(context_info->isolate);
	HandleScope handle_scope(context_info->isolate);

	Local<String> eval = String::NewFromUtf8(context_info->isolate, RSTRING_PTR(str),
						  NewStringType::kNormal, (int)RSTRING_LEN(str)).ToLocalChecked();

	eval_params.context_info = context_info;
	eval_params.eval = &eval;
	eval_params.result = &eval_result;
	eval_params.timeout = 0;
	VALUE timeout = rb_iv_get(self, "@timeout");
	if (timeout != Qnil) {
	    eval_params.timeout = (useconds_t)NUM2LONG(timeout);
	}

	eval_result.message = NULL;
	eval_result.backtrace = NULL;

	rb_thread_call_without_gvl(nogvl_context_eval, &eval_params, unblock_eval, &eval_params);

	if (eval_result.message != NULL) {
	    Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.message);
	    message = convert_v8_to_ruby(context_info->isolate, tmp);
	    eval_result.message->Reset();
	    delete eval_result.message;
	}

	if (eval_result.backtrace != NULL) {
	    Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.backtrace);
	    backtrace = convert_v8_to_ruby(context_info->isolate, tmp);
	    eval_result.backtrace->Reset();
	    delete eval_result.backtrace;
	}
    }

    // NOTE: this is very important, we can not do an rb_raise from within
    // a v8 scope, if we do the scope is never cleaned up properly and we leak
    if (!eval_result.parsed) {
	if(TYPE(message) == T_STRING) {
	    rb_raise(rb_eParseError, "%s", RSTRING_PTR(message));
	} else {
	    rb_raise(rb_eParseError, "Unknown JavaScript Error during parse");
	}
    }

    if (!eval_result.executed) {

	VALUE ruby_exception = rb_iv_get(self, "@current_exception");
	if (ruby_exception == Qnil) {
	    ruby_exception = eval_result.terminated ? rb_eScriptTerminatedError : rb_eScriptRuntimeError;
	    // exception report about what happened
	    if(TYPE(message) == T_STRING && TYPE(backtrace) == T_STRING) {
		rb_raise(ruby_exception, "%s/n%s", RSTRING_PTR(message), RSTRING_PTR(backtrace));
	    } else if(TYPE(message) == T_STRING) {
		rb_raise(ruby_exception, "%s", RSTRING_PTR(message));
	    } else {
		rb_raise(ruby_exception, "Unknown JavaScript Error during execution");
	    }
	} else {
            VALUE rb_str = rb_funcall(ruby_exception, rb_intern("to_s"), 0);
	    rb_raise(CLASS_OF(ruby_exception), "%s", RSTRING_PTR(rb_str));
	}
    }

    // New scope for return value
    {
	Locker lock(context_info->isolate);
	Isolate::Scope isolate_scope(context_info->isolate);
	HandleScope handle_scope(context_info->isolate);

	Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.value);
	result = convert_v8_to_ruby(context_info->isolate, tmp);

	eval_result.value->Reset();
	delete eval_result.value;
    }

    return result;
}
#endif

#ifdef RUBY_COMPILE
typedef struct {
    VALUE callback;
    int length;
    VALUE* args;
    bool failed;
} protected_callback_data;
#endif

#ifdef RUBY_COMPILE
static
VALUE protected_callback(VALUE rdata) {
    protected_callback_data* data = (protected_callback_data*)rdata;
    VALUE result;

    if (data->length > 0) {
	result = rb_funcall2(data->callback, rb_intern("call"), data->length, data->args);
    } else {
	result = rb_funcall(data->callback, rb_intern("call"), 0);
    }
    return result;
}
#endif

#ifdef RUBY_COMPILE
static
VALUE rescue_callback(VALUE rdata, VALUE exception) {
    protected_callback_data* data = (protected_callback_data*)rdata;
    data->failed = true;
    return exception;
}
#endif

#ifdef RUBY_COMPILE
void*
gvl_ruby_callback(void* data) {

    FunctionCallbackInfo<Value>* args = (FunctionCallbackInfo<Value>*)data;
    VALUE* ruby_args;
    int length = args->Length();
    VALUE callback;
    VALUE result;
    VALUE self;

    {
	HandleScope scope(args->GetIsolate());
	Handle<External> external = Handle<External>::Cast(args->Data());

	VALUE* self_pointer = (VALUE*)(external->Value());
	self = *self_pointer;
	callback = rb_iv_get(self, "@callback");

	if (length > 0) {
	    ruby_args = ALLOC_N(VALUE, length);
	}


	for (int i = 0; i < length; i++) {
	    Local<Value> value = ((*args)[i]).As<Value>();
	    ruby_args[i] = convert_v8_to_ruby(args->GetIsolate(), value);
	}
    }

    // may raise exception stay clear of handle scope
    protected_callback_data callback_data;
    callback_data.length = length;
    callback_data.callback = callback;
    callback_data.args = ruby_args;
    callback_data.failed = false;

    result = rb_rescue((VALUE(*)(...))&protected_callback, (VALUE)(&callback_data),
			(VALUE(*)(...))&rescue_callback, (VALUE)(&callback_data));

    if(callback_data.failed) {
	VALUE parent = rb_iv_get(self, "@parent");
	rb_iv_set(parent, "@current_exception", result);
	args->GetIsolate()->ThrowException(String::NewFromUtf8(args->GetIsolate(), "Ruby exception"));
    }
    else {
	HandleScope scope(args->GetIsolate());
	Handle<Value> v8_result = convert_ruby_to_v8(args->GetIsolate(), result);
	args->GetReturnValue().Set(v8_result);
    }

    if (length > 0) {
	xfree(ruby_args);
    }

    return NULL;
}
#endif

#ifdef RUBY_COMPILE
static void ruby_callback(const FunctionCallbackInfo<Value>& args) {
    rb_thread_call_with_gvl(gvl_ruby_callback, (void*)(&args));
}
#endif


#ifdef RUBY_COMPILE
static VALUE rb_external_function_notify_v8(VALUE self) {

    ContextInfo* context_info;

    VALUE parent = rb_iv_get(self, "@parent");
    VALUE name = rb_iv_get(self, "@name");
    VALUE parent_object = rb_iv_get(self, "@parent_object");
    VALUE parent_object_eval = rb_iv_get(self, "@parent_object_eval");

    bool parse_error = false;
    bool attach_error = false;

    Data_Get_Struct(parent, ContextInfo, context_info);

    {
	Locker lock(context_info->isolate);
	Isolate::Scope isolate_scope(context_info->isolate);
	HandleScope handle_scope(context_info->isolate);

	Local<Context> context = context_info->context->Get(context_info->isolate);
	Context::Scope context_scope(context);

	Local<String> v8_str = String::NewFromUtf8(context_info->isolate, RSTRING_PTR(name),
						  NewStringType::kNormal, (int)RSTRING_LEN(name)).ToLocalChecked();

	// copy self so we can access from v8 external
	VALUE* self_copy;
	Data_Get_Struct(self, VALUE, self_copy);
	*self_copy = self;

	Local<Value> external = External::New(context_info->isolate, self_copy);

	if (parent_object == Qnil) {
	    context->Global()->Set(v8_str, FunctionTemplate::New(context_info->isolate, ruby_callback, external)->GetFunction());
	} else {

	    Local<String> eval = String::NewFromUtf8(context_info->isolate, RSTRING_PTR(parent_object_eval),
						      NewStringType::kNormal, (int)RSTRING_LEN(parent_object_eval)).ToLocalChecked();

	    MaybeLocal<Script> parsed_script = Script::Compile(context, eval);
	    if (parsed_script.IsEmpty()) {
		parse_error = true;
	    } else {
		MaybeLocal<Value> maybe_value = parsed_script.ToLocalChecked()->Run(context);
		attach_error = true;

		if (!maybe_value.IsEmpty()) {
		    Local<Value> value = maybe_value.ToLocalChecked();
		    if (value->IsObject()){
			value.As<Object>()->Set(v8_str, FunctionTemplate::New(context_info->isolate, ruby_callback, external)->GetFunction());
			attach_error = false;
		    }
		}
	    }
	}
    }

    // always raise out of V8 context
    if (parse_error) {
	rb_raise(rb_eParseError, "Invalid object %s", RSTRING_PTR(parent_object));
    }

    if (attach_error) {
	rb_raise(rb_eParseError, "Was expecting %s to be an object", RSTRING_PTR(parent_object));
    }

    return Qnil;
}
#endif

void deallocate(void * data) {
    ContextInfo* context_info = (ContextInfo*)data;
    {
	Locker lock(context_info->isolate);
    }

    {
	context_info->context->Reset();
	delete context_info->context;
    }

    {
	if (context_info->interrupted) {
	    fprintf(stderr, "WARNING: V8 isolate was interrupted by Ruby, it can not be disposed and memory will not be reclaimed till the Ruby process exits.");
	} else {
	    context_info->isolate->Dispose();
	}
    }

    delete context_info->allocator;
#ifdef RUBY_COMPILE
    xfree(context_info);
#else
    free(context_info);
#endif
}

#ifdef RUBY_COMPILE
void deallocate_external_function(void * data) {
    xfree(data);
}
#endif

#ifdef RUBY_COMPILE
VALUE allocate_external_function(VALUE klass) {
    VALUE* self = ALLOC(VALUE);
    return Data_Wrap_Struct(klass, NULL, deallocate_external_function, (void*)self);
}
#endif

#ifdef RUBY_COMPILE
VALUE allocate(VALUE klass) {
    init_v8();

    ContextInfo* context_info = ALLOC(ContextInfo);
    context_info->allocator = new ArrayBufferAllocator();
    context_info->interrupted = false;
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = context_info->allocator;

    context_info->isolate = Isolate::New(create_params);

    Locker lock(context_info->isolate);
    Isolate::Scope isolate_scope(context_info->isolate);
    HandleScope handle_scope(context_info->isolate);

    Local<Context> context = Context::New(context_info->isolate);

    context_info->context = new Persistent<Context>();
    context_info->context->Reset(context_info->isolate, context);

    return Data_Wrap_Struct(klass, NULL, deallocate, (void*)context_info);
}
#endif

#ifdef RUBY_COMPILE
static VALUE
rb_context_stop(VALUE self) {
    ContextInfo* context_info;
    Data_Get_Struct(self, ContextInfo, context_info);
    V8::TerminateExecution(context_info->isolate);
    return Qnil;
}
#endif

#ifdef PYTHON_COMPILE
// Python version of allocate()
ContextInfo *PyMiniRacer_init_context() {
    init_v8();

    ContextInfo* context_info = ALLOC(ContextInfo);
    context_info->allocator = new ArrayBufferAllocator();
    context_info->interrupted = false;
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = context_info->allocator;

    context_info->isolate = Isolate::New(create_params);

    Locker lock(context_info->isolate);
    Isolate::Scope isolate_scope(context_info->isolate);
    HandleScope handle_scope(context_info->isolate);

    Local<Context> context = Context::New(context_info->isolate);

    context_info->context = new Persistent<Context>();
    context_info->context->Reset(context_info->isolate, context);

    return context_info;
    // return Data_Wrap_Struct(klass, NULL, deallocate, (void*)context_info);
    
}
#endif

#ifdef PYTHON_COMPILE
#if 1
PythonValue* PyMiniRacer_eval_context_unsafe(ContextInfo *context_info, char *str) {
// static VALUE rb_context_eval_unsafe(VALUE self, VALUE str)

    EvalParams eval_params;
    EvalResult eval_result;

    PythonValue *result = NULL;

    PythonValue *message = NULL;
    PythonValue *backtrace = NULL;

    if (context_info == NULL) {
        return NULL;
    }

    if (str == NULL) {
        return NULL;
    }

    {
	Locker lock(context_info->isolate);
	Isolate::Scope isolate_scope(context_info->isolate);
	HandleScope handle_scope(context_info->isolate);

    Local<String> eval = String::NewFromUtf8(context_info->isolate, str,
						  NewStringType::kNormal, (int)strlen(str)).ToLocalChecked();

	eval_params.context_info = context_info;
	eval_params.eval = &eval;
	eval_params.result = &eval_result;
	eval_params.timeout = 0;
    // FIXME - no support for timeout
	// VALUE timeout = rb_iv_get(self, "@timeout");
	// if (timeout != Qnil) {
	//     eval_params.timeout = (useconds_t)NUM2LONG(timeout);
	// }

	eval_result.message = NULL;
	eval_result.backtrace = NULL;

    // Ruby has a convenient way to call execution while releasing the GIL
    // https://github.com/ruby/ruby/blob/06c57968707ba5a09e6347237fd289673269247b/thread.c#L1313
	// rb_thread_call_without_gvl(nogvl_context_eval, &eval_params, unblock_eval, &eval_params);
    nogvl_context_eval(&eval_params);

	if (eval_result.message != NULL) {
	    Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.message);
	    message = convert_v8_to_python(context_info->isolate, tmp);
	    eval_result.message->Reset();
	    delete eval_result.message;
	}

	if (eval_result.backtrace != NULL) {
	    Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.backtrace);
	    backtrace = convert_v8_to_python(context_info->isolate, tmp);
	    eval_result.backtrace->Reset();
	    delete eval_result.backtrace;
	}
    }

    // NOTE: this is very important, we can not do an rb_raise from within
    // a v8 scope, if we do the scope is never cleaned up properly and we leak
    if (!eval_result.parsed) {
        result = ALLOC(PythonValue);
        result->type = type_exception;
        // FIXME: is the value being freed in all time here?
        if(message && TYPE(message) == T_STRING) {
            result->value = strdup(PSTRING_PTR(message));
        } else {
            result->value = strdup("Unknown JavaScript Error during parse");
        }
        return result;
    }

    if (!eval_result.executed) {
        result = ALLOC(PythonValue);
        result->type = type_exception;

        if(message && TYPE(message) == T_STRING) {
            result->value = PSTRING_PTR(message);
        } else {
            result->value = strdup("Unknown JavaScript Error during parse");
        }

        if(TYPE(message) == T_STRING && TYPE(backtrace) == T_STRING) {
            char *msg_str = PSTRING_PTR(message);
            char *backtrace_str = PSTRING_PTR(backtrace);
            size_t dest_size = strlen(msg_str) + strlen(backtrace_str) + 1;
            char *dest = (char *) malloc(dest_size);
            if (!dest) {
                PythonValueFree(message);
                return NULL;
            }
            snprintf(dest, dest_size, "%s\n%s", msg_str, backtrace_str);

            result->value = dest;
            result->len = dest_size;
        } else if(TYPE(message) == T_STRING) {
            result->value = strdup(PSTRING_PTR(message));
        } else {
            result->value = strdup("Unknown JavaScript Error during execution");
        }
        PythonValueFree(message);
        PythonValueFree(backtrace);

        return result;
    }
    PythonValueFree(message);
    PythonValueFree(backtrace);

    // New scope for return value
    {
	Locker lock(context_info->isolate);
	Isolate::Scope isolate_scope(context_info->isolate);
	HandleScope handle_scope(context_info->isolate);

	Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.value);
	result = convert_v8_to_python(context_info->isolate, tmp);

	eval_result.value->Reset();
	delete eval_result.value;
    }

    return result;
}
#endif // #if 0
#endif

extern "C" {

#ifdef PYTHON_COMPILE
PythonValue* pmr_eval_context(ContextInfo *context_info, char *str) {
    PythonValue *res = PyMiniRacer_eval_context_unsafe(context_info, str);
    return res;
}

ContextInfo *pmr_init_context() {
    ContextInfo *res =  PyMiniRacer_init_context();
    return res;
}

void pmr_free_value(PythonValue *val) {
    PythonValueFree(val);
}

void pmr_free_context(ContextInfo *context_info) {
    deallocate(context_info);
}
#endif

#ifdef RUBY_COMPILE
    void Init_mini_racer_extension ( void )
    {
	VALUE rb_mMiniRacer = rb_define_module("MiniRacer");
	VALUE rb_cContext = rb_define_class_under(rb_mMiniRacer, "Context", rb_cObject);

	VALUE rb_eEvalError = rb_define_class_under(rb_mMiniRacer, "EvalError", rb_eStandardError);
	rb_eScriptTerminatedError = rb_define_class_under(rb_mMiniRacer, "ScriptTerminatedError", rb_eEvalError);
	rb_eParseError = rb_define_class_under(rb_mMiniRacer, "ParseError", rb_eEvalError);
	rb_eScriptRuntimeError = rb_define_class_under(rb_mMiniRacer, "RuntimeError", rb_eEvalError);
	rb_cJavaScriptFunction = rb_define_class_under(rb_mMiniRacer, "JavaScriptFunction", rb_cObject);

	VALUE rb_cExternalFunction = rb_define_class_under(rb_cContext, "ExternalFunction", rb_cObject);
	rb_define_method(rb_cContext, "stop", (VALUE(*)(...))&rb_context_stop, 0);
	rb_define_alloc_func(rb_cContext, allocate);

	rb_define_private_method(rb_cContext, "eval_unsafe",(VALUE(*)(...))&rb_context_eval_unsafe, 1);
	rb_define_private_method(rb_cExternalFunction, "notify_v8", (VALUE(*)(...))&rb_external_function_notify_v8, 0);
	rb_define_alloc_func(rb_cExternalFunction, allocate_external_function);
    }
#endif

}
