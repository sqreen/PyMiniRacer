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


PickleSerializer::PickleSerializer(Isolate * isolate, Local<Context> context):
	isolate_(isolate), context_(context) {
	WriteProto();
}

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
    if (!ExpandBuffer(new_size).To(&ok) || !ok) {
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

Maybe<std::pair<uint8_t *, size_t>> PickleSerializer::Release() {
	WriteStop();
	if (out_of_memory_) {
		return Nothing<std::pair<uint8_t *, size_t>>();
	}
	auto result = std::make_pair(buffer_, buffer_size_);
	buffer_ = nullptr;
	buffer_size_ = 0;
	buffer_capacity_ = 0;
	return Just(result);
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

void PickleSerializer::WriteInt(int32_t i) {
	WriteOpCode(PickleOpCode::kBinInt);
	char * raw = reinterpret_cast<char *>(&i);
#if V8_TARGET_BIG_ENDIAN
	std::reverse(raw, raw + sizeof(i));
#endif
	WriteRawBytes(raw, sizeof(i));
}

void PickleSerializer::WriteInt32(Int32 * value) {
	WriteInt(value->Value());
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

Maybe<bool> PickleSerializer::WriteBigInt(BigInt * value) {
	int wc = value->WordCount();
	if (wc > static_cast<int>(UINT32_MAX / sizeof(uint64_t) - 1)) {
		return Nothing<bool>();
	}
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
	return Just(true);
}

void PickleSerializer::WriteString(Local<String> value) {
	int length = value->Utf8Length(isolate_);
	uint8_t * dest;

	WriteOpCode(PickleOpCode::kBinUnicode);
	WriteSize(length);
	if (ReserveRawBytes(length).To(&dest) && length > 0) {
		value->WriteUtf8(isolate_, reinterpret_cast<char *>(dest), length, nullptr, String::WriteOptions::NO_NULL_TERMINATION);
	}
}

void PickleSerializer::WriteBatchContent(Local<Array> value, PickleOpCode op) {
	size_t length = value->Length();
	uint32_t i = 0;

	while (i < length) {
		WriteOpCode(PickleOpCode::kMark);
		for (int j = 0; i < length && j < 1000; ++i, ++j) {
			Local<Value> item;

			if (value->Get(context_, i).ToLocal(&item)) {
				WriteValue(item);
			} else {
				WriteNone();
			}
		}
		WriteOpCode(op);
	}
}

Maybe<bool> PickleSerializer::WriteObject(Local<Object> value) {
	TryCatch trycatch(isolate_);
	Context::Scope scope(context_);

	int hash = value->GetIdentityHash();
	std::pair<uint32_t, Local<Object>>& memoed = memo_[hash];

	if (memoed.first) {
		WriteOpCode(PickleOpCode::kLongBinGet);
		WriteSize(memoed.first - 1);
		return Just(true);
	}

	if (memo_.size() > UINT32_MAX - 1) {
		return Nothing<bool>();
	}
	memoed.first = static_cast<uint32_t>(memo_.size());
	memoed.second = value;

	if (value->IsArray()) {
		WriteOpCode(PickleOpCode::kEmptyList);
		WriteOpCode(PickleOpCode::kLongBinPut);
		WriteSize(memoed.first - 1);
		WriteBatchContent(Local<Array>::Cast(value), PickleOpCode::kAppends);
	} else if (value->IsMap()) {
		WriteOpCode(PickleOpCode::kEmptyDict);
		WriteOpCode(PickleOpCode::kLongBinPut);
		WriteSize(memoed.first - 1);

		WriteBatchContent(Local<Map>::Cast(value)->AsArray(), PickleOpCode::kSetItems);
	} else {
		WriteOpCode(PickleOpCode::kEmptyDict);
		WriteOpCode(PickleOpCode::kLongBinPut);
		WriteSize(memoed.first - 1);

		Local<Array> keys;
		if (value->GetOwnPropertyNames(context_).ToLocal(&keys)) {
			for (uint32_t i = 0; i < keys->Length(); ++i) {
				Local<Value> key;
				Local<Value> item;

				if (!keys->Get(context_, i).ToLocal(&key)
				    || !value->Get(context_, key).ToLocal(&item)) {
					return Nothing<bool>();
				}
				WriteValue(key);
				WriteValue(item);
				WriteOpCode(PickleOpCode::kSetItem);
			}
		}
	}
	return (out_of_memory_) ? Nothing<bool>() : Just(true);
}

void PickleSerializer::WriteDate(Date * value) {
	// TODO check local time vs UTC
	std::time_t dt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::duration<double, std::milli>(value->ValueOf()))));
	std::tm tm = *std::gmtime(&dt);

	WriteOpCode(PickleOpCode::kGlobal);
#define DATETIME "datetime\ndatetime\n"
	WriteRawBytes(DATETIME, sizeof(DATETIME) - 1);
#undef DATETIME
	WriteOpCode(PickleOpCode::kMark);
	WriteInt(tm.tm_year + 1900);
	WriteInt(tm.tm_mon + 1);
	WriteInt(tm.tm_mday);
	WriteInt(tm.tm_hour);
	WriteInt(tm.tm_min);
	WriteInt(tm.tm_sec);
	WriteOpCode(PickleOpCode::kTuple);
	WriteOpCode(PickleOpCode::kReduce);
}

Maybe<bool> PickleSerializer::WriteValue(Local<Value> value) {
	if (value->IsNull() || value->IsUndefined()) {
		WriteNone();
	} else if (value->IsBoolean()) {
		WriteBoolean(Boolean::Cast(*value));
	} else if (value->IsInt32()) {
		WriteInt32(Int32::Cast(*value));
	} else if (value->IsNumber()) {
		WriteNumber(Number::Cast(*value));
	} else if (value->IsString()) {
		WriteString(Local<String>::Cast(value));
	} else if (value->IsSymbol()) {
		WriteValue(Symbol::Cast(*value)->Name());
	} else if (value->IsDate()) {
		WriteDate(Date::Cast(*value));
	} else if (value->IsBigInt()) {
		return WriteBigInt(BigInt::Cast(*value));
	} else if (value->IsObject()) {
		return WriteObject(Local<Object>::Cast(value));
	} else {
		return Just(false);
	}
	return (out_of_memory_) ? Nothing<bool>() : Just(true);
}

Maybe<bool> PickleSerializer::WriteException(char const * name, Local<Value> exception, Local<Value> stacktrace)
{
	WriteOpCode(PickleOpCode::kGlobal);
#define MODULE "py_mini_racer.py_mini_racer\n"
	WriteRawBytes(MODULE, sizeof(MODULE) - 1);
#undef MODULE
	WriteRawBytes(name, strlen(name));
	WriteRawBytes("\n", 1);
	if (exception->IsString()) {
		WriteString(Local<String>::Cast(exception));
		WriteOpCode(PickleOpCode::kTuple1);
	} else {
		WriteOpCode(PickleOpCode::kNone);
	}
	WriteOpCode(PickleOpCode::kNone);
	WriteOpCode(PickleOpCode::kTuple3);
	return (out_of_memory_) ? Nothing<bool>() : Just(true);
}

BinaryValue * convert_v8_to_pickle(Isolate * isolate,
				   Local<Context> context,
				   Local<Value> value)
{
	Isolate::Scope isolate_scope(isolate);
	HandleScope scope(isolate);
	PickleSerializer serializer = PickleSerializer(isolate, context);
	serializer.WriteValue(value);

	BinaryValue *res = new (xalloc(res)) BinaryValue();
	std::pair<uint8_t *, size_t> ret;
	if (serializer.Release().To(&ret)) {
		res->type = type_pickle;
		res->buf = std::get<0>(ret);
		res->len = std::get<1>(ret);
	} else {
		res->type = type_invalid;
	}
	return res;
}

BinaryValue *convert_v8_to_binary(Isolate * isolate,
		                  Local<Context> context,
                                  Local<Value> value)
{
	Isolate::Scope isolate_scope(isolate);
	HandleScope scope(isolate);

	return convert_v8_to_pickle(isolate, context, value);
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
