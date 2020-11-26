#include "PreCompiled.h"
#include "RefCountedObject.h"

RefCountedObject::RefCountedObject()
	: StrongReferences(0)
{
	AddRef();
}

RefCountedObject::~RefCountedObject()
{
}

uint32 RefCountedObject::AddRef()
{
	return ++StrongReferences;
}

uint32 RefCountedObject::Release()
{
	uint32 NewRefCount = --StrongReferences;
	if (StrongReferences <= 0)
	{
		delete this;
	}

	return NewRefCount;
}
