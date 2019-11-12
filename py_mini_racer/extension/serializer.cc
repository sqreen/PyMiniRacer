#include "serializer.h"

// V8_TARGET_LITTLE_ENDIAN
#include "src/base/build_config.h"

namespace v8 {

void BinaryValueFree(BinaryValue *v) {
    if (!v) {
        return;
    }
    switch(v->type) {
    case type_execute_exception:
    case type_parse_exception:
    case type_oom_exception:
    case type_timeout_exception:
    case type_str_utf8:
        free(v->str_val);
        break;
    case type_array:
        for (size_t i = 0; i < v->len; i++) {
            BinaryValue *w = v->array_val[i];
            BinaryValueFree(w);
        }
        free(v->array_val);
        break;
    case type_hash:
        for(size_t i = 0; i < v->len; i++) {
            BinaryValue *k = v->hash_val[i*2];
            BinaryValue *w = v->hash_val[i*2+1];
            BinaryValueFree(k);
            BinaryValueFree(w);
        }
        free(v->hash_val);
        break;
    case type_bool:
    case type_double:
    case type_date:
    case type_null:
    case type_integer:
    case type_function: // no value implemented
    case type_symbol:
    case type_invalid:
        // the other types are scalar values
        break;
    case type_pickle:
	free(v->buf);
	break;
    }
    free(v);
}


PickleSerializer::PickleSerializer(Isolate * isolate):
	isolate_(isolate) {}

PickleSerializer::~PickleSerializer() {
	if (buffer_) {
		free(buffer_);
	}
}

Maybe<bool> PickleSerializer::ExpandBuffer(size_t required_capacity) {
  //DCHECK_GT(required_capacity, buffer_capacity_);
  size_t requested_capacity =
      std::max(required_capacity, buffer_capacity_ * 2) + 64;
  size_t provided_capacity = 0;
  void* new_buffer = nullptr;
  new_buffer = realloc(buffer_, requested_capacity);
  provided_capacity = requested_capacity;
  if (new_buffer) {
    //DCHECK(provided_capacity >= requested_capacity);
    buffer_ = reinterpret_cast<uint8_t*>(new_buffer);
    buffer_capacity_ = provided_capacity;
    return Just(true);
  } else {
    out_of_memory_ = true;
    return Nothing<bool>();
  }
}


Maybe<uint8_t *> PickleSerializer::ReserveRawBytes(size_t bytes) {
  size_t old_size = buffer_size_;
  size_t new_size = old_size + bytes;
  if (new_size > buffer_capacity_) {
    bool ok;
    if (!ExpandBuffer(new_size).To(&ok)) {
      return Nothing<uint8_t *>();
    }
  }
  buffer_size_ = new_size;
  return Just(&buffer_[old_size]);
}


void PickleSerializer::WriteRawBytes(void const * source, size_t length) {
  uint8_t * dest;
  if (ReserveRawBytes(length).To(&dest) && length > 0) {
    memcpy(dest, source, length);
  }
}


std::pair<uint8_t *, size_t> PickleSerializer::Release() {
	Isolate::Scope isolate_scope(isolate_);
	auto result = std::make_pair(buffer_, buffer_size_);
	buffer_ = nullptr;
	buffer_size_ = 0;
	buffer_capacity_ = 0;
	return result;
}

void PickleSerializer::WriteOpCode(PickleOpCode code) {
  uint8_t raw_code = static_cast<uint8_t>(code);
  WriteRawBytes(&raw_code, sizeof(raw_code));
}

static const uint8_t kPickleProto = 2;

void PickleSerializer::WriteProto() {
	WriteOpCode(PickleOpCode::kProto);
	WriteRawBytes(&kPickleProto, sizeof(kPickleProto));
}

void PickleSerializer::WriteStop() {
	WriteOpCode(PickleOpCode::kStop);
}

void PickleSerializer::WriteNone() {
	WriteOpCode(PickleOpCode::kNone);
}

void PickleSerializer::WriteBoolean(Boolean * value) {
	if (value->Value()) {
		WriteOpCode(PickleOpCode::kTrue);
	} else {
		WriteOpCode(PickleOpCode::kFalse);
	}
}

void PickleSerializer::WriteInt32(Int32 * value) {
	int32_t tmp = value->Value();
	WriteOpCode(PickleOpCode::kBinInt);
	char * raw = reinterpret_cast<char *>(&tmp);
#if V8_TARGET_BIG_ENDIAN
	std::reverse(raw, raw + sizeof(tmp));
#endif
	WriteRawBytes(raw, sizeof(tmp));
}

void PickleSerializer::WriteNumber(Number * value) {
	double tmp = value->Value();
	char * raw = reinterpret_cast<char *>(&tmp);
#if V8_TARGET_LITTLE_ENDIAN
	std::reverse(raw, raw + sizeof(tmp));
#endif
	WriteOpCode(PickleOpCode::kBinFloat);
	WriteRawBytes(raw, sizeof(tmp));
}

void PickleSerializer::WriteSize(uint32_t size) {
	char * raw = reinterpret_cast<char *>(&size);
#if V8_TARGET_BIG_ENDIAN
	std::reverse(raw, raw + sizeof(size));
#endif
	WriteRawBytes(raw, sizeof(size));
}

void PickleSerializer::WriteBigInt(BigInt * value) {
	int wc = value->WordCount();
	// TODO integer overflow check
	uint32_t length = wc * sizeof(uint64_t) + 1;
	int negative = 0;
	uint8_t * dest;

	WriteOpCode(PickleOpCode::kLong4);
	WriteSize(length);
	if (ReserveRawBytes(length).To(&dest)) {
		uint64_t * raw = reinterpret_cast<uint64_t *>(dest);
		value->ToWordsArray(&negative, &wc, raw);
		if (negative) {
			// 2's complement
			while (--wc >= 0) {
				*(raw + wc) = ~(*(raw + wc));
			}
			*raw += 1;
			dest[length - 1] = 0xFF;
		} else {
			dest[length - 1] = 0x00;
		}
	}
}

BinaryValue * convert_v8_to_pickle(Isolate * isolate,
				   Local<Context> context,
				   Local<Value> value)
{
	Isolate::Scope isolate_scope(isolate);
	HandleScope scope(isolate);
	PickleSerializer serializer = PickleSerializer(isolate);

	serializer.WriteProto();
	if (value->IsNull() || value->IsUndefined()) {
		serializer.WriteNone();
	} else if (value->IsBoolean()) {
		serializer.WriteBoolean(Boolean::Cast(*value));
	} else if (value->IsInt32()) {
		serializer.WriteInt32(Int32::Cast(*value));
	} else if (value->IsNumber()) {
		serializer.WriteNumber(Number::Cast(*value));
	} else if (value->IsBigInt()) {
		serializer.WriteBigInt(BigInt::Cast(*value));
	}
	// TODO Throw an error if allocation failed
	serializer.WriteStop();

	auto ret = serializer.Release();
	BinaryValue *res = new (xalloc(res)) BinaryValue();
        res->type = type_pickle;
        res->buf = std::get<0>(ret);
	res->len = std::get<1>(ret);
	return res;
}

BinaryValue *convert_v8_to_binary(Isolate * isolate,
		                  Local<Context> context,
                                  Local<Value> value)
{
	Isolate::Scope isolate_scope(isolate);
    HandleScope scope(isolate);

    BinaryValue *res = new (xalloc(res)) BinaryValue();

    if (value->IsNull()
	|| value->IsUndefined()
	|| value->IsBoolean()
	|| value->IsInt32()
	|| value->IsNumber()
	|| value->IsBigInt()) {
	return convert_v8_to_pickle(isolate, context, value);
    }

    else if (value->IsArray()) {
        Local<Array> arr = Local<Array>::Cast(value);
        size_t len = arr->Length();
        BinaryValue **ary = xalloc(ary, sizeof(*ary) * len);

        res->type = type_array;
        res->array_val = ary;

        for(uint32_t i = 0; i < arr->Length(); i++) {
            Local<Value> element = arr->Get(context, i).ToLocalChecked();
            BinaryValue *bin_value = convert_v8_to_binary(isolate, context, element);
            if (bin_value == NULL) {
                goto err;
            }
            ary[i] = bin_value;
            res->len++;
        }
    }

    else if (value->IsFunction()){
        res->type = type_function;
    }

    else if (value->IsSymbol()){
        res->type = type_symbol;
    }

    else if (value->IsDate()) {
        res->type = type_date;
        Local<Date> date = Local<Date>::Cast(value);

        double timestamp = date->ValueOf();
        res->double_val = timestamp;
    }

    else if (value->IsObject()) {
        res->type = type_hash;

        TryCatch trycatch(isolate);

        Local<Object> object = value->ToObject(context).ToLocalChecked();
        MaybeLocal<Array> maybe_props = object->GetOwnPropertyNames(context);
        if (!maybe_props.IsEmpty()) {
            Local<Array> props = maybe_props.ToLocalChecked();
            uint32_t hash_len = props->Length();

            if (hash_len > 0) {
                res->hash_val = xalloc(res->hash_val,
                                       sizeof(*res->hash_val) * hash_len * 2);
            }

            for (uint32_t i = 0; i < hash_len; i++) {

                MaybeLocal<Value> maybe_pkey = props->Get(context, i);
                if (maybe_pkey.IsEmpty()) {
			goto err;
		}
		Local<Value> pkey = maybe_pkey.ToLocalChecked();
                MaybeLocal<Value> maybe_pvalue = object->Get(context, pkey);
                // this may have failed due to Get raising
                if (maybe_pvalue.IsEmpty() || trycatch.HasCaught()) {
                    // TODO: factor out code converting exception in
                    //       nogvl_context_eval() and use it here/?
                    goto err;
                }

                BinaryValue *bin_key = convert_v8_to_binary(isolate, context, pkey);
                BinaryValue *bin_value = convert_v8_to_binary(isolate, context, maybe_pvalue.ToLocalChecked());

                if (!bin_key || !bin_value) {
                    BinaryValueFree(bin_key);
                    BinaryValueFree(bin_value);
                    goto err;
                }

                res->hash_val[i * 2]     = bin_key;
                res->hash_val[i * 2 + 1] = bin_value;
                res->len++;
            }
        } // else empty hash
    }

    else {
        Local<String> rstr = value->ToString(context).ToLocalChecked();

        res->type = type_str_utf8;
        res->len = size_t(rstr->Utf8Length(isolate)); // in bytes
        size_t capacity = res->len + 1;
        res->str_val = xalloc(res->str_val, capacity);
        rstr->WriteUtf8(isolate, res->str_val);
    }
    return res;

err:
    BinaryValueFree(res);
    return NULL;
}


BinaryValue *convert_v8_to_binary(Isolate * isolate,
		                  const Persistent<Context> & context,
                                  Local<Value> value)
{
    HandleScope scope(isolate);
    return convert_v8_to_binary(isolate,
                                Local<Context>::New(isolate, context),
                                value);
}

}
