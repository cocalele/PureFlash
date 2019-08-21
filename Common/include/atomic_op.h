#ifndef __ATOMIC_OP_H__
#define __ATOMIC_OP_H__

#include "basetype.h"
#include "machine.h"

template <typename T>
class AtomicType
{
private:
	volatile T value_;

public:
	AtomicType(T v = 0) : value_(v)
	{}

	AtomicType(AtomicType &v )
	{
		value_ = v.value_;
	}

	operator T ()
	{
		return MachineExchangeAdd<T>(&value_, 0);
	}

	T operator ()()
	{
		return MachineExchangeAdd<T>(&value_, 0);
	}


	AtomicType& operator = (T v)
	{
		MachineExchange<T>(&value_, v);
		return *this;
	}

	AtomicType& operator = (AtomicType& v)
	{
		MachineExchange<T>(&value_, v.value_);
		return *this;
	}

	T compare_exchange(T to_exchange, T to_compare)
	{
		return MachineCompareExchange<T>(&value_, to_exchange, to_compare);
	}

	T fetch_and_add(T addend)
	{
		return MachineExchangeAdd<T>(&value_,addend);
	}

	T fetch_and_store(T val)
	{
		return MachineExchange<T>(&value_, val);
	}

	T operator++()
	{
		// prefix
		return MachineIncrement<T>(&value_);
	}

	T operator++(int)
	{
		//postfix
		T before(value_);
		MachineIncrement<T>(&value_);
		return before;
	}

	T operator--()
	{
		// prefix
		return MachineDecrement<T>(&value_);
	}

	T operator--(int)
	{
		//postfix
		T before(value_);
		MachineDecrement<T>(&value_);
		return before;
	}

	T operator+=(T right)
	{
		return MachineExchangeAdd<T>(&value_, right);
	}

	T operator+=(const AtomicType& right)
	{
		return MachineExchangeAdd<T>(&value_, right.value_);
	}

	T operator-=(T right)
	{
		return MachineExchangeSub<T>(&value_, right);
	}

	T operator-=(AtomicType& right)
	{
		return MachineExchangeSub<T>(&value_, right.value_);
	}


	int operator == (AtomicType& rhs)
	{
		return value_ == rhs.value_;
	}


	int operator == (T rhs)
	{
		return value_ == rhs;
	}

	int operator < (AtomicType& rhs)
	{
		return value_ < rhs.value_;
	}

	int operator != (AtomicType& rhs)
	{
		return value_ != rhs.value_;
	}

	int operator >= (AtomicType& rhs)
	{
		return value_ >= rhs.value_;
	}

	int operator > (AtomicType& rhs)
	{
		return value_ > rhs.value_;
	}

	int operator<= (AtomicType& rhs)
	{
		return value_ <= rhs.value_;
	}

	T value()
	{
		return value_ ;
	}

};

typedef AtomicType<char>	AtomicInt8;
typedef AtomicType<uchar>	AtomicUInt8;
typedef AtomicType<int16>	AtomicInt16;
typedef AtomicType<uint16>	AtomicUInt16;
typedef AtomicType<int32>	AtomicInt32;
typedef AtomicType<uint32>	AtomicUInt32;
typedef AtomicType<int64>	AtomicInt64;
typedef AtomicType<uint64>	AtomicUInt64;

#define atomic AtomicType


#endif //__ATOMIC_OP_H__
