#pragma once
#include "OmniBin.h"

// TArray
template<typename T>
inline void omniLoad(TArray<T>& object, OmniBin<std::ios::in>& omniBin)
{
	uint64 s;
	omniLoad(s, omniBin);
	object.SetNum(s);

	if constexpr (serializeAsPOD<T>)
	{
		if (s > 0)
			omniBin.stream.read(reinterpret_cast<char*>(&object[0]), sizeof(T) * s);
	}
	else
	{
		for (size_t i = 0; i < s; ++i)
		{
			auto&& item = object[i];
			omniLoad(item, omniBin);
		}
	}
}

// TMap
template<typename K, typename V>
inline void omniLoad(TMap<K, V>& object, OmniBin<std::ios::in>& omniBin)
{
	uint64 s;
	omniLoad(s, omniBin);

	for (uint64 i = 0; i < s; ++i)
	{
		std::pair<K, V> keyValue;
		auto&& [k, v] = keyValue;
		omniLoad(k, omniBin);
		omniLoad(v, omniBin);
		object.FindOrAdd(k) = v;
	}
}

// FString
inline void omniLoad(FString& object, OmniBin<std::ios::in>& omniBin)
{
	std::string tmp;
	omniLoad(tmp, omniBin);
	object = FString(tmp.data());
}