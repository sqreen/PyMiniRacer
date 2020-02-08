#pragma once
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <map>
#include <ratio>

#include <v8.h>

namespace v8 {

enum BinaryTypes {
    type_invalid   =   0,
    type_null      =   1,
    type_bool      =   2,
    type_integer   =   3,
    type_double    =   4,
    type_str_utf8  =   5,
    type_array     =   6,
    type_hash      =   7,
    type_date      =   8,
    type_symbol    =   9,

    type_function  = 100,

    type_execute_exception = 200,
    type_parse_exception   = 201,
    type_oom_exception = 202,
    type_timeout_exception = 203,

    type_pickle = 999,
};

/* This is a generic store for arbitrary JSON like values.
 * Non scalar values are:
 *  - Strings: pointer to a string
 *  - Arrays: contiguous map of pointers to BinaryValue
 *  - Hash: contiguous map of pair of pointers to BinaryTypes (first is key,
 *          second is value)
 */
struct BinaryValue {
    union {
        BinaryValue **array_val;
        BinaryValue **hash_val;
        char *str_val;
        uint32_t int_val;
        double double_val;
	uint8_t * buf;
    };
    enum BinaryTypes type = type_invalid;
    size_t len;
};

void BinaryValueFree(BinaryValue *v);
BinaryValue *convert_v8_to_binary(Isolate * isolate,
		                  Local<Context> context,
                                  Local<Value> value);
BinaryValue *convert_v8_to_binary(Isolate * isolate,
		                  const Persistent<Context> & context,
                                  Local<Value> value);

template<class T> static inline T* xalloc(T*& ptr, size_t x = sizeof(T))
{
    void *tmp = malloc(x);
    if (tmp == NULL) {
        fprintf(stderr, "malloc failed. Aborting");
        abort();
    }
    ptr = static_cast<T*>(tmp);
    return static_cast<T*>(ptr);
}

enum class PickleOpCode : uint8_t {
	// version:uint8_t (2 by default)
	kProto = 0x80,
	// Pop the top element of the stack and return it
	kStop = '.',
	// no parameters
	kNone = 'N',
	kTrue = 0x88,
	kFalse = 0x89,
	// value:int32_t
	kBinInt = 'J',
	// value:double
	kBinFloat = 'G',
	// size:uint32_t long:size * bytes
	kLong4 = 0x8B,
	// length:uint32_t string: length * bytes
	kBinUnicode = 'X',
	// index:int32_t
	kLongBinGet = 'j',
	// index:int32_t
	kLongBinPut = 'r',
	kEmptyList = ']',
	kMark = '(',
	kAppends = 'e',
	kEmptyDict = '}',
	kSetItems = 'u',
	kSetItem = 's',
	// module:line object:line
	kGlobal = 'c',
	// callable:cls tuple:args
	kReduce = 'R',
	kTuple = 't',
	kTuple1 = 0x85,
	kTuple2 = 0x86,
	kTuple3 = 0x87,
};


class PickleSerializer
{
	public:
		PickleSerializer(Isolate * isolate, Local<Context> context);
		~PickleSerializer();

		Maybe<bool> WriteValue(Local<Value> value);
		Maybe<bool> WriteObject(Local<Object> value);
		Maybe<std::pair<uint8_t *, size_t>> Release();
		Maybe<bool> WriteException(char const * name, Local<Value> exception, Local<Value> stacktrace);

	private:
		void WriteSize(uint32_t size);
		void WriteInt(int32_t i);

		void WriteProto();
		void WriteStop();

		void WriteNone();
		void WriteBoolean(Boolean * boolean);
		void WriteInt32(Int32 * value);
		void WriteNumber(Number * value);
		void WriteDate(Date * value);
		void WriteString(Local<String> value);

		void WriteBatchContent(Local<Array> value, PickleOpCode op);
		void WriteOpCode(PickleOpCode code);
		void WriteRawBytes(void const * source, size_t length);

		Maybe<bool> WriteBigInt(BigInt * value);

		Maybe<uint8_t *> ReserveRawBytes(size_t bytes);
		Maybe<bool> ExpandBuffer(size_t required_capacity);

		Isolate * const isolate_;
		Local<Context> context_;
		std::map<int, std::pair<uint32_t, Local<Object>>> memo_;
		uint8_t * buffer_ = nullptr;
		size_t buffer_size_ = 0;
		size_t buffer_capacity_ = 0;
		bool out_of_memory_ = false;
};

}
