/*
 * @(#)AsnRuntime.h
 *
 * Copyright (c) 2009-2020, ASN Lab
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 	1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 	2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 	3. Neither the name of ASN Lab nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef ASNRUNTIME_H_
#define ASNRUNTIME_H_


#include <sstream>
#include <cstring>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <map>
#if __cplusplus >= 201703L
#include <optional>
#include <variant>
#endif
#ifdef _WIN64
   // Windows (64-bit)
#elif _WIN32
   // Windows (32-bit)
#elif __APPLE__
	#include <optional>
	#include <variant>
#elif __linux
    // linux
#elif __unix // all unices not caught above
    // Unix
#elif __posix
    // POSIX
#endif
#include "BigIntegerLibrary.hh"

using namespace std;

namespace asnrt {

#define		ASNRT_VERSION		"v3.14.15.r20220921"

/**
 * C++ STL offers a special implementation of vector< bool >, which make its elements non-addressable.
 * Hence we define our own version of boolean to make vector container consistent.
 **/
typedef unsigned char	boolean;
#define TRUE	1
#define FALSE	0

#ifdef _MSC_VER /* VS */
	typedef __int64 xlong;
	typedef unsigned __int64 uxlong;
	#define NL "\r\n"
#else /* Unix */
	typedef long long xlong;
	typedef unsigned long long uxlong;
	#define NL "\n"
#endif

#ifndef offsetof
#define offsetof(type, field) ((size_t)(&((type *)0)->field))
#endif /* offsetof */

template< typename T>
class static_constructable {
private:
	struct helper {
		helper()	 {
			T::static_constructor();
		}
	};
protected:
	static_constructable() {
		static helper placeholder;
	}
};

/* The encoding rules */

#define		BASIC_ENCODING_RULES             0
#define		CANONICAL_ENCODING_RULES         1
#define		DISTINGUISHED_ENCODING_RULES     2
#define		UNALIGNED_PACKED_ENCODING_RULES  3
#define		ALIGNED_PACKED_ENCODING_RULES    4

// C++ values types for ASN.1 values

typedef std::basic_string<bool> bitstring;

typedef string	octetstring;

typedef std::basic_string<wchar_t> wstring;

typedef std::basic_string<unsigned int> ustring;

typedef std::basic_string<size_t> objectid;

class Date {
private:
	xlong fastTime;
	struct tm breakTime;
	int milliseconds;

public:
	Date();
	Date(xlong time);
	Date(int year, int month, int date, int hours, int minutes, int seconds, int milleseconds);

	int getYear(); /* 1970~2037 */
	int getMonth(); /* 1~12 */
	int getDate();  /* 1~31 */
	int getHours(); /* 0~23 */
	int getMinutes(); /* 0~59 */
	int getSeconds(); /* 0~59 */
	int getMilliSeconds(); /* 0~999 */

	xlong getTime();

	Date& toUTCTime();
	Date& fromUTCTime(int year, int month, int date, int hours, int minutes, int seconds, int milleseconds);

	bool operator==(const Date &t) const;

private:
	int getTimeZoneOffset();
};

// ASN.1 Exception

/* error codes */
#define		INVALID_TAG                     1
#define		INVALID_LENGTH                  2
#define		INVALID_INTEGER                 3
#define		INVALID_ENUM                    4
#define		INVALID_REAL                    5
#define		OUT_OF_MEMORY                   6
#define		INVALID_TIME                    7
#define		MISSING_COMPONENT               8
#define		EXTRA_COMPONENT                 9
#define		INVALID_INDEX                   10
#define		BUFFER_OVERFLOW                 11
#define		BUFFER_UNDERFLOW                12
#define		INVALID_ENCODE_RULE             13
#define		NULL_POINTER_EXCEPTION          14
#define		NOT_PERMITTED_ALPHABET          15
#define		NO_MATCH_INFO_OBJ               16
#define		INVALID_LICENSE                 17
#define		INVALID_SIZE                    18
#define		IO_EXCEPTION                    19
#define		UNKNOWN_EXCEPTION               20
#define		ABSTRACT_FUNCTION               21

#ifdef _DEBUG_
	#define ASN_EXCEPTION(errorCode, message) { AsnException ex(__FILE__, __LINE__, errorCode, message); throw ex; }
	#define ASN_ERROR(errorCode) { AsnException ex(__FILE__, __LINE__, errorCode); throw ex; }
#else
	#define ASN_EXCEPTION(errorCode, message) { AsnException ex(errorCode, message); throw ex; }
	#define ASN_ERROR(errorCode) { AsnException ex(errorCode); throw ex; }
#endif

class AsnException {
public:
	static string err_msgs[];

private:
	int errorCode;
	stringstream message;

public:
	AsnException(char* file, int line, int errorCode, string message);
	AsnException(char* file, int line, int errorCode);
	AsnException(int errorCode, string message);
	AsnException(int errorCode);
	AsnException(const AsnException& o);
	int getErrorCode();
	string getMessage();
	AsnException& operator<<(long __n);
	AsnException& operator<<(unsigned long __n);
	AsnException& operator<<(bool __n);
	AsnException& operator<<(short __n);
	AsnException& operator<<(unsigned short __n);
	AsnException& operator<<(int __n);
	AsnException& operator<<(unsigned int __n);
#ifdef _GLIBCXX_USE_LONG_LONG
	AsnException& operator<<(long long __n);
	AsnException& operator<<(unsigned long long __n);
#endif
	AsnException& operator<<(double __f);
	AsnException& operator<<(float __f);
	AsnException& operator<<(long double __f);
	AsnException& operator<<(const void* __p);
#ifndef _LIBCPP_CXX03_LANG
	AsnException& operator<<(stringstream __sb);
#endif  // _LIBCPP_CXX03_LANG
	AsnException& operator<<(string __s);
	AsnException& operator<<(char* __cstr);
};

// ASN.1 converters

struct AsnConverter {
	virtual ~AsnConverter() {}
};

struct BooleanConverter: public AsnConverter {
	static BooleanConverter INSTANCE;
	boolean toValue(void* object);
	void toObject(void* object, boolean value);
};

struct NullConverter: public AsnConverter {
	static NullConverter INSTANCE;
};

class AsnBuffer;
class IntegerType;
struct IntegerConverter : public AsnConverter {
	virtual ~IntegerConverter() {}
	virtual BigInteger toValue(void* object) = 0;
	virtual void toObject(void* object, BigInteger bi) = 0;
	virtual void ber_encode(AsnBuffer* buffer, void* object, IntegerType* type) = 0;
	virtual void ber_decode(AsnBuffer* buffer, void* object, IntegerType* type) = 0;
	virtual void per_encode(AsnBuffer* buffer, void* object, IntegerType* type) = 0;
	virtual void per_decode(AsnBuffer* buffer, void* object, IntegerType* type) = 0;
};

struct IntConverter : public IntegerConverter {
	static IntConverter INSTANCE;
	BigInteger toValue(void* object);
	void toObject(void* object, BigInteger bi);
	void ber_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void ber_decode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_decode(AsnBuffer* buffer, void* object, IntegerType* type);
};

struct LongConverter : public IntegerConverter {
	static LongConverter INSTANCE;
	BigInteger toValue(void* object);
	void toObject(void* object, BigInteger bi);
	void ber_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void ber_decode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_decode(AsnBuffer* buffer, void* object, IntegerType* type);
};

struct XLongConverter : public IntegerConverter {
	static XLongConverter INSTANCE;
	BigInteger toValue(void* object);
	void toObject(void* object, BigInteger bi);
	void ber_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void ber_decode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_decode(AsnBuffer* buffer, void* object, IntegerType* type);
};

struct BigIntegerConverter : public IntegerConverter {
	static BigIntegerConverter INSTANCE;
	BigInteger toValue(void* object);
	void toObject(void* object, BigInteger bi);
	void ber_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void ber_decode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_encode(AsnBuffer* buffer, void* object, IntegerType* type);
	void per_decode(AsnBuffer* buffer, void* object, IntegerType* type);
};

struct AbstractEnumeratedConverter : public AsnConverter {
	virtual ~AbstractEnumeratedConverter() {}
	virtual int toValue(void* object) = 0;
	virtual void toObject(void* object, int value) = 0;
};

template<typename _Tp>
struct EnumeratedConverter: public AbstractEnumeratedConverter {

	int toValue(void* object) {
		_Tp* _tp = static_cast<_Tp*>(object);
		int value = *_tp;
		return value;
	}

	void toObject(void* object, int value) {
		::new(object) _Tp(value);
	}

};

struct RealConverter: public AsnConverter {
	virtual ~RealConverter() {}
	virtual double toValue(void* object) = 0;
	virtual void toObject(void* object, double value) = 0;
};

struct FloatConverter: RealConverter {
	static FloatConverter INSTANCE;
	double toValue(void* object);
	void toObject(void* object, double value);
};

struct DoubleConverter: RealConverter {
	static DoubleConverter INSTANCE;
	double toValue(void* object);
	void toObject(void* object, double value);
};

struct BitStringConverter: public AsnConverter {
	static BitStringConverter INSTANCE;
	bitstring* toBitString(void* object);
};

struct OctetStringConverter: public AsnConverter {
	static OctetStringConverter INSTANCE;
	octetstring* toOctetString(void* object);
};

struct ObjectIdConverter: public AsnConverter {
	static ObjectIdConverter INSTANCE;
	objectid* toObjectid(void* object);
};

struct StringConverter: public AsnConverter {
	static StringConverter INSTANCE;
	string* toString(void* object);
};

struct WStringConverter: public AsnConverter {
	static WStringConverter INSTANCE;
	wstring* toString(void* object);
};

struct UStringConverter: public AsnConverter {
	static UStringConverter INSTANCE;
	ustring* toString(void* object);
};

struct DateConverter : public AsnConverter {
	static DateConverter INSTANCE;
	Date* toDate(void* object);
};

struct ListConverter : public AsnConverter {
	AsnConverter* componentConverter;
	ListConverter(): componentConverter(NULL) {}
	ListConverter(AsnConverter* componentConverter): componentConverter(componentConverter) {}

	virtual ~ListConverter() {}

	/**
	 *  Returns the number of elements in the list.
	 **/
	virtual size_t getSize(void* listObject) = 0;

	/**
	 *  Removes all of the elements from this list.
	 **/
	virtual void clearAll(void* listObject) = 0;

	/**
	 *  Adds a new element to the list.
	 **/
	virtual void* addElement(void* listObject) = 0;

	/**
	 * A new element has been decoded completed
	 **/
	virtual void doneElement(void* listObject, void* element) = 0;

    /**
     * Returns the component at the specified index.
     */
	virtual void* elementAt(void* listObject, size_t index) = 0;
};

template<typename _Tp>
struct VectorConverter : public ListConverter {
#if __cplusplus > 201103L
	using vector_type = vector<_Tp>;
#else
	typedef vector<_Tp> vector_type;
#endif
	VectorConverter() : ListConverter() {}
	VectorConverter(AsnConverter* componentConverter) : ListConverter(componentConverter) {}
	virtual ~VectorConverter() {}

	size_t getSize(void* listObject) {
		vector_type* _vector = static_cast<vector_type*>(listObject);
		return _vector->size();
	}

	void clearAll(void* listObject) {
		vector_type* _vector = static_cast<vector_type*>(listObject);
		_vector->clear();
	}

	void* addElement(void* listObject) {
		vector_type* _vector = static_cast<vector_type*>(listObject);
#if __cplusplus >= 201103L
		_Tp t {};
#else
		_Tp t = _Tp();
#endif
		_vector->push_back(t);
		_Tp& ref = _vector->back();
		return &ref;
	}

	void doneElement(void* listObject, void* element) {};

	void* elementAt(void* listObject, size_t index) {
		vector_type* _vector = static_cast<vector_type*>(listObject);
		_Tp& ref =_vector->at(index);
		return &ref;
	}

};

struct AsnAccessor {
	virtual ~AsnAccessor() {};
	virtual void* get(void* object) = 0;
	virtual void* create(void* object) = 0;
	virtual void free(void* object) {}
};

struct UnitAccessor: public AsnAccessor {
	static UnitAccessor INSTANCE;
	~UnitAccessor() {};
	inline void* get(void* object) { return object; }
	inline void* create(void* object) { return object; }
};

#if __cplusplus >= 201703L
template<typename _Tp>
struct OptionalAccessor : public AsnAccessor {
	using optional_type = std::optional<_Tp>;
	virtual ~OptionalAccessor() {}
	inline void* get(void* object) {
		optional_type* o = static_cast<optional_type*>(object);
		if(o->has_value()) {
			_Tp& ref = o->value();
			return (void*) &ref;
		}
		else {
			return NULL;
		}
	}
	inline void* create(void* object) {
		_Tp t {};
		optional_type* o = static_cast<optional_type*>(object);
		o->emplace(t);
		_Tp& ref = o->value();
		return (void*) &ref;
	}
};
#endif

#if __cplusplus >= 201703L
template<typename _Variant, size_t _Np>
struct VariantAccessor: public AsnAccessor {
	~VariantAccessor() {};
	inline void* get(void* object) {
		_Variant* v = static_cast<_Variant*>(object);
		auto* p = std::get_if<_Np>(v);
		return p;
	}
	inline void* create(void* object) {
		_Variant* v = static_cast<_Variant*>(object);
		v->template emplace<_Np>();
		auto* p = std::get_if<_Np>(v);
		return p;
	}
};
#endif

template<typename _Tp>
struct PointerAccessor: public AsnAccessor {
	~PointerAccessor() {};
	inline void* get(void* object) {
		_Tp* vp = *(static_cast<_Tp**>(object));
		return vp;
	}
	inline void* create(void* object) {
		_Tp* _tp =  new _Tp();
		void* vp = _tp;
		*(void**) object = vp;
		return vp;
	}
	inline void free(void* object) {
		void* vp = *(void**) object;
		if(vp != NULL) {
			_Tp* _tp =  (_Tp*) vp;
			delete _tp;
			*(void**) object = NULL;
		}
	}
};

struct MemberDescriptor {
	int offset;
	AsnConverter* converter;
	AsnAccessor* accessor;

	void* get(void* composite) {
		void* p = (char*)composite + offset;
		void* member = accessor->get(p);
	    return member;
	}

	void* create(void* composite) {
	    void* p = (char*)composite + offset;
	    void* member = accessor->create(p);
		return member;
	}

	void free(void* composite) {
		void* p = (char*)composite + offset;
		accessor->free(p);
	}
};

struct CompositeConverter : public AsnConverter {
	MemberDescriptor* componentDescriptors;
	CompositeConverter(): componentDescriptors(NULL) {}
	CompositeConverter(MemberDescriptor* componentDescriptors): componentDescriptors(componentDescriptors) {}
	virtual ~CompositeConverter() {}
	virtual AsnConverter* getComponentConverter(size_t index);
	virtual void* getComponentObject(void* object, size_t index);
	virtual void* createComponentObject(void* object, size_t index);
	virtual void doneComponentObject(void* object, size_t index);
};

struct AlternativeDescriptor {
	AsnConverter* converter;
	AsnAccessor* accessor;
	void* get(void* choice) {
		return accessor->get(choice);
	}
	void* create(void* choice) {
		return accessor->create(choice);
	}
	void free(void* choice) {
		accessor->free(choice);
	}
};

struct ChoiceConverter : public AsnConverter {
	AlternativeDescriptor* alternativeDescriptors;
	ChoiceConverter(): alternativeDescriptors(NULL) {}
	ChoiceConverter(AlternativeDescriptor* alternativeDescriptors): alternativeDescriptors(alternativeDescriptors) {}
	virtual ~ChoiceConverter() {}
	virtual void* getAlternativeObject(void* object);
	virtual void* createAlternativeObject(void* object, size_t index);
	virtual size_t getAlternativeOffset(void* object) { return 0; }
	virtual size_t getAlternativeIndex(void* object) = 0;
};

#if __cplusplus >= 201703L
template<typename _Variant>
struct VariantChoiceConverter : public ChoiceConverter {
	VariantChoiceConverter(): ChoiceConverter() {}
	VariantChoiceConverter(AlternativeDescriptor* alternativeDescriptors): ChoiceConverter(alternativeDescriptors) {}
	virtual ~VariantChoiceConverter() {}
	inline size_t getAlternativeIndex(void* object) {
		_Variant* variant = static_cast<_Variant*>(object);
		return variant->index();
	}
};
#endif

template<typename _Union>
struct UnionChoiceConverter : public ChoiceConverter {
	UnionChoiceConverter() {}
	UnionChoiceConverter(AlternativeDescriptor* alternativeDescriptors): ChoiceConverter(alternativeDescriptors) {}
	virtual ~UnionChoiceConverter() {}
	inline void* createAlternativeObject(void* object, size_t index) {
		_Union* _union = static_cast<_Union*>(object);
		_union->choice = index;
		return ChoiceConverter::createAlternativeObject(object, index);
	}
	inline size_t getAlternativeOffset(void* object) {
		return offsetof(_Union, value);
	}
	inline size_t getAlternativeIndex(void* object) {
		_Union* _union = static_cast<_Union*>(object);
		return _union->choice;
	}
};

struct VariantDescriptor {
	int index;
	AsnConverter* converter;
	AsnAccessor* accessor;
	void* get(void* variant) {
		return accessor->get(variant);
	}
	void* create(void* variant) {
		return accessor->create(variant);
	}
	void free(void* variant) {
		accessor->free(variant);
	}
};

struct OpenValueConverter : public AsnConverter {
	void* objectSet;
	ListConverter* objectSetConverter;
	VariantDescriptor* variantDescriptors;
	size_t	numOfVariants;
	OpenValueConverter(void* objectSet, ListConverter* objectSetConverter, VariantDescriptor* variantDescriptors, size_t n):
		objectSet(objectSet), objectSetConverter(objectSetConverter), variantDescriptors(variantDescriptors), numOfVariants(n)  {}
	virtual ~OpenValueConverter() {}
	VariantDescriptor* getVariantDescriptor(int index);
};

// Useful macros and templates

#define DECLARE_TYPE() \
public:	static AsnType* TYPE

#define DECLARE_COMPOSITE_CONVERTER(ConverterClass) \
private: static MemberDescriptor _MemberDescriptors_[]; \
public:	 static ConverterClass CONVERTER

#define DECLARE_CHOICE_CONVERTER(ConverterClass) \
private: static AlternativeDescriptor _AlternativeDescriptors_[]; \
public:	 static ConverterClass CONVERTER

#define DECLARE_OPEN_CONVERTER(component) \
private: \
	static VariantDescriptor _##component##AcutalValueDescriptors_[]; 	\
	static OpenValueConverter _##component##Converter

#define ASN_COMPONENT(CompositeClass, component, converter, accessor) { offsetof(CompositeClass, component), converter, accessor }
#define ASN_ALTERNATIVE(converter, accessor) { converter, accessor }
#define ASN_VARIANT(index, converter, accessor) { index, converter, accessor }

#define ASN_COMPOSITE(ConverterClass, CompositeClass, ...) 	\
	MemberDescriptor CompositeClass::_MemberDescriptors_[] = {	\
		__VA_ARGS__ \
	};	\
	ConverterClass CompositeClass::CONVERTER(_MemberDescriptors_)

#define ASN_CHOICE(ConverterClass, ChoiceClass, ...) \
	AlternativeDescriptor ChoiceClass::_AlternativeDescriptors_[] = {	\
		__VA_ARGS__ \
	};	\
	ConverterClass ChoiceClass::CONVERTER(_AlternativeDescriptors_)

#define ASN_OPEN_TYPE(CompositeClass, component, objectSet, ObjectSetConverter, ...) \
	VariantDescriptor CompositeClass::_##component##AcutalValueDescriptors_[] = {	\
		__VA_ARGS__ \
	};	\
	OpenValueConverter CompositeClass::_##component##Converter(objectSet, ObjectSetConverter, _##component##AcutalValueDescriptors_, sizeof(_##component##AcutalValueDescriptors_)/sizeof(VariantDescriptor))


// Public API

class AsnBuffer;
class AsnType;

struct AsnTriplet {
	void* object;
	AsnType* type;
	AsnConverter* converter;
	int typeId;
};

/* Buffer Allocation */

/**
 * Wraps an array into a buffer, basically, this buffer is for decoding.
 *
 * The new buffer will be backed by the given byte array;
 * that is, modifications to the buffer will cause the array to be modified and vice versa.
 * For Basic Encoding Rules, the new buffer's capacity and limit will be <tt>numBytes</tt>,
 * and for Pack Encoding Rules, the new buffer's capacity and limit will be <tt>numBytes*8</tt>.
 */
AsnBuffer* wrap_buffer(char* array, unsigned long numBytes, char encodingRules);

/**
 * Allocates a new buffer, basically, this buffer is for encoding.
 *
 * For Basic Encoding Rules, the new buffer's capacity and limit will be <tt>numBytes</tt>,
 * and for Pack Encoding Rules, the new buffer's capacity and limit will be <tt>numBytes*8</tt>.
 */
AsnBuffer* alloc_buffer(unsigned long numBytes, bool autoExpand, char encodingRules);

/**
 * Create a buffer out of an istream, this buffer is for decoding.
 */
AsnBuffer* istream_buffer(istream* in, unsigned long numBytes, char encodingRules);

/**
 * Create a buffer out of an ostream, this buffer is for encoding.
 */
AsnBuffer* ostream_buffer(ostream* out, unsigned long numBytes, char encodingRules);

/**
 * Get the buffer's encoding content
 */
void buffer_content(AsnBuffer* buffer, octetstring* content);

/**
 * Free a buffer. The memory of byte array and the buffer itself will
 * be freed.
 */
void free_buffer(AsnBuffer*& buffer);


/* Encoding/Decoding Routines */

/**
 * Encode the object with encoding rules specified in the buffer
 **/
void encode_object(AsnBuffer* buffer, void* object, AsnType* type, AsnConverter* converter);

/**
 * Decode the object with encoding rules specified in the buffer
 **/
void decode_object(AsnBuffer* buffer, void* object, AsnType* type, AsnConverter* converter);


/*  Utilities Routines */

/**
 * print the object as ASN.1 value
 **/
void print_object(const void* object, AsnType* type, AsnConverter* converter, ostream* out, bool trimNulls=false);

/**
 * determine if the two object equal as ASN.1 value
 **/
bool equals_to(const void* o1, const void* o2, AsnType* type, AsnConverter* converter);

/**
 * deep clone object o2 to o1 as ASN.1 value
 * that is o1 = o2
 **/
void clone_object(const void* o1, const void* o2, AsnType* type, AsnConverter* converter);

/**
 * deep free object content
 **/
void free_object(void* object, AsnType* type, AsnConverter* converter);


// Miscellaneous

class TypeEntry {
public:
	long id;
	string name;
	AsnType* type;
	TypeEntry(): id(0), type(NULL) {};
	TypeEntry(long id, string name, AsnType* type): id(id), name(name), type(type) {}
};

class ValueEntry {
public:
	long tid;
	long vid;
	octetstring value;

	AsnType* type;
};

class ModuleEntry {
public:
	vector<TypeEntry> types;
	vector<ValueEntry> values;
};

class AsnModule {
private:
	ModuleEntry module;
	map<long, TypeEntry> types;
	map<long, ValueEntry> values;

public:
	AsnModule();
	AsnModule(const char* metafile);
	virtual ~AsnModule();
	void load(const char* metafile);
	void unload();
	AsnType* getType(long id);
	int getValue(void* value, long valueId, AsnConverter* converter);
	int getValueSet(void* valueSet, long valueSetId, AsnConverter* valueSetConverter);
	AsnType* TYPE;
	AsnConverter* CONVERTER;
protected:
	bool loaded;

private:
	void decode(octetstring& octets);
	void buildLookupTable();
	void linkTypesHierarchy();
	AsnType* linkTypeHierarchy(AsnType* type, map<AsnType*, AsnType*>& dejavu);
	void linkObjectHierachy(void* object, AsnType* type, AsnConverter* converter, map<AsnType*, AsnType*>& dejavu);
};

// ~END

}

#endif /*ASNRUNTIME_H_*/
