//#include "stdafx.h"
#include "OmniBin.h"
#include "Containers/UnrealString.h"

//void omniSave(const FString& object, OmniBin<std::ios::out>& omniBin)
//{
//    QByteArray BA = object.toUtf8();
//    omniSave(BA.size(), omniBin);
//    omniBin.stream.write(reinterpret_cast<const char*>(BA.data()), BA.size());
//}

//void omniLoad(FString& object, OmniBin<std::ios::in>& omniBin)
//{
//    TArray<uint8> BA;
//    if(!FFileHelper::LoadFileToArray(BA, ))
//    int s;
//    omniLoad(s, omniBin);
//    BA.resize(s);
//
//    omniBin.stream.read(reinterpret_cast<char*>(BA.data()), BA.size());
//    object = FString::fromUtf8(BA);
//}
