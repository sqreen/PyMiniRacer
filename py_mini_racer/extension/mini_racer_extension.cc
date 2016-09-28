
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

static void * xalloc(size_t x) {
    void *tmp = malloc(x);
    if (tmp == NULL) {
        abort();
    }
    return tmp;
}
#define ALLOC(x) ((x *) xalloc(sizeof(x)));

#define TYPE(x) x->type
#define PSTRING_PTR(x) ((char *) x->value)

enum BinaryTypes {
    type_null      =   1,
    type_bool      =   2,
    type_integer   =   3,
    type_double    =   4,
    type_str_utf8  =   5,
    type_array     =   6,
    type_hash      =   7,
    type_date      =   8,

    type_function  = 100,

    type_execute_exception = 200,
    type_parse_exception   = 201,

    type_invalid   = 300,
};

#define T_STRING type_str_utf8

/* This is a generic store for arbitrary JSON like values.
 * Non scalar values are:
 *  - Strings: pointer to a string
 *  - Arrays: contiguous map of pointers to BinaryValue
 *  - Hash: contiguous map of pair of pointers to BinaryTypes (first is key,
 *          second is value)
 */
typedef struct {
    void *value;
    enum BinaryTypes type;
    size_t len;
} BinaryValue;


void BinaryValueFree(BinaryValue *v) {
    size_t i=0;
    if (!v) {
        return;
    }
    switch(v->type) {
        case type_str_utf8:
            free(v->value);
            break;
        case type_array:
            for(i=0; i < v->len; i++) {
                BinaryValue *w = ((BinaryValue **) v->value)[i];
                BinaryValueFree(w);
            }
            break;
        case type_hash:
            for(i=0; i < v->len; i++) {
                BinaryValue *k = ((BinaryValue **) v->value)[i*2];
                BinaryValue *w = ((BinaryValue **) v->value)[i*2+1];
                BinaryValueFree(k);
                BinaryValueFree(w);
            }
            break;
        default:
            // the other types are scalar values
            break;
    }
    free(v);
}


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

void* nogvl_context_eval(void* arg) {
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


BinaryValue *convert_v8_to_binary(ContextInfo *context_info,
                                  Handle<Value> &value) {

    Local<Context> context = context_info->context->Get(context_info->isolate);

    Context::Scope context_scope(context);

    Isolate *isolate = context_info->isolate;
    HandleScope scope(isolate);

    BinaryValue *res = ALLOC(BinaryValue);
    res->len = 0;
    res->value = NULL;
    res->type = type_invalid;

    if (value->IsNull() || value->IsUndefined()) {
        res->type = type_null;
        res->value = NULL;
        return res;
    }

    if (value->IsInt32()) {
        res->type = type_integer;
        uint32_t val = value->Int32Value();
        *(uint32_t *) &res->value = val;
        return res;
    }

    // ECMA-262, 4.3.20
    // http://www.ecma-international.org/ecma-262/5.1/#sec-4.3.19
    if (value->IsNumber()) {
        res->type = type_double;
        double val = value->NumberValue();
        *(double *) &res->value = val;
        return res;
    }

    if (value->IsBoolean()) {
        res->type = type_bool;
        *(int *) &res->value = (value->IsTrue() ? 1 : 0);
        return res;
    }

    if (value->IsArray()) {
        Local<Array> arr = Local<Array>::Cast(value);
        size_t len = arr->Length();
        BinaryValue **ary = (BinaryValue **) malloc(sizeof(BinaryValue *) * len);
        if (!ary) {
            free(res);
            return NULL;
        }

        for(uint32_t i=0; i < arr->Length(); i++) {
            Local<Value> element = arr->Get(i);
            BinaryValue *bin_value = convert_v8_to_binary(context_info, element);
            if (bin_value == NULL) {
                free(res);
                free(ary);
                return NULL;
            }
            ary[i] = bin_value;
        }
        res->type = type_array;
        res->len = len;
        res->value = (void *) ary;
        return res;
    }

    if (value->IsFunction()){
        res->type = type_function;
        res->value = NULL;
        res->len = 0;
        return res;
    }

    if (value->IsDate()) {
        res->type = type_date;
        Local<Date> date = Local<Date>::Cast(value);

        double timestamp = date->ValueOf();

        *(double *) &res->value = timestamp;
        return res;
    }

    if (value->IsObject()) {
        res->type = type_hash;

        TryCatch trycatch(isolate);

        Local<Object> object = value->ToObject();
        MaybeLocal<Array> maybe_props = object->GetOwnPropertyNames(context);
        if (!maybe_props.IsEmpty()) {
            Local<Array> props = maybe_props.ToLocalChecked();
            uint32_t hash_len = props->Length();

            res->len = hash_len;
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
                Local<Value> value = object->Get(key);
                // this may have failed due to Get raising

                if (trycatch.HasCaught()) {
                    // TODO isolate code that translates execption

                    free(res->value);
                    free(res);
                    return NULL;
                }

                BinaryValue *bin_key = convert_v8_to_binary(context_info, key);
                BinaryValue *bin_value = convert_v8_to_binary(context_info, value);

                ((BinaryValue **) res->value)[i*2]   = bin_key;
                ((BinaryValue **) res->value)[i*2+1] = bin_value;
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

    int str_len = rstr->Utf8Length();
    res->len = str_len;
    str_len++;
    res->value = (char *) malloc(str_len);
    if (!res->value) {
        free(res);
        return NULL;
    }
    rstr->WriteUtf8((char *) res->value, str_len);
    return res;
}


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
    free(context_info);
}


ContextInfo *MiniRacer_init_context() {
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
}

BinaryValue* MiniRacer_eval_context_unsafe(ContextInfo *context_info,
                                           char *utf_str,
                                           int   str_len) {

    EvalParams eval_params;
    EvalResult eval_result;

    BinaryValue *result = NULL;

    BinaryValue *message = NULL;
    BinaryValue *backtrace = NULL;

    if (context_info == NULL) {
        return NULL;
    }

    if (utf_str == NULL) {
        return NULL;
    }

    {
        Locker lock(context_info->isolate);
        Isolate::Scope isolate_scope(context_info->isolate);
        HandleScope handle_scope(context_info->isolate);

        Local<String> eval = String::NewFromUtf8(context_info->isolate,
                                                 utf_str,
                                                 NewStringType::kNormal,
                                                 str_len).ToLocalChecked();

        eval_params.context_info = context_info;
        eval_params.eval = &eval;
        eval_params.result = &eval_result;
        eval_params.timeout = 0;

        // FIXME - we should allow setting a timeout here
        //     eval_params.timeout = (useconds_t)NUM2LONG(timeout);

        eval_result.message = NULL;
        eval_result.backtrace = NULL;

        nogvl_context_eval(&eval_params);

        if (eval_result.message != NULL) {
            Local<Value> tmp = Local<Value>::New(context_info->isolate,
                                                 *eval_result.message);
            message = convert_v8_to_binary(context_info, tmp);
            eval_result.message->Reset();
            delete eval_result.message;
        }

        if (eval_result.backtrace != NULL) {
            Local<Value> tmp = Local<Value>::New(context_info->isolate,
                                                 *eval_result.backtrace);
            backtrace = convert_v8_to_binary(context_info, tmp);
            eval_result.backtrace->Reset();
            delete eval_result.backtrace;
        }
    }

    // NOTE: this is very important, we can not do an raise from within
    // a v8 scope, if we do the scope is never cleaned up properly and we leak
    if (!eval_result.parsed) {
        result = ALLOC(BinaryValue);
        result->type = type_parse_exception;
        // FIXME: is the value being freed in all time here?
        if(message && TYPE(message) == T_STRING) {
            result->value = strdup(PSTRING_PTR(message));
        } else {
            result->value = strdup("Unknown JavaScript error during parse");
        }
        return result;
    }

    if (!eval_result.executed) {
        result = ALLOC(BinaryValue);
        result->type = type_execute_exception;

        if(message && TYPE(message) == T_STRING && backtrace && TYPE(backtrace) == T_STRING) {
            char *msg_str = PSTRING_PTR(message);
            char *backtrace_str = PSTRING_PTR(backtrace);
            size_t dest_size = strlen(msg_str) + strlen(backtrace_str) + 1;
            char *dest = (char *) malloc(dest_size);
            if (!dest) {
                BinaryValueFree(message);
                free(result);
                return NULL;
            }
            snprintf(dest, dest_size, "%s\n%s", msg_str, backtrace_str);

            result->value = dest;
            result->len = dest_size;
        } else if(message && TYPE(message) == T_STRING) {
            result->value = strdup(PSTRING_PTR(message));
        } else {
            result->value = strdup("Unknown JavaScript error during execution");
        }
        BinaryValueFree(message);
        BinaryValueFree(backtrace);

        return result;
    }
    BinaryValueFree(message);
    BinaryValueFree(backtrace);

    // New scope for return value
    {
        Locker lock(context_info->isolate);
        Isolate::Scope isolate_scope(context_info->isolate);
        HandleScope handle_scope(context_info->isolate);

        Local<Value> tmp = Local<Value>::New(context_info->isolate, *eval_result.value);
        result = convert_v8_to_binary(context_info, tmp);

        eval_result.value->Reset();
        delete eval_result.value;
    }

    return result;
}

extern "C" {

BinaryValue* mr_eval_context(ContextInfo *context_info, char *str, int len) {
    BinaryValue *res = MiniRacer_eval_context_unsafe(context_info, str, len);
    return res;
}

ContextInfo *mr_init_context() {
    ContextInfo *res = MiniRacer_init_context();
    return res;
}

void mr_free_value(BinaryValue *val) {
    BinaryValueFree(val);
}

void mr_free_context(ContextInfo *context_info) {
    deallocate(context_info);
}


}
