#pragma once

#include "7zip/IArchive.h"

struct CNesFile
{
	unsigned char hdr[16];
	std::string fname;
};

class CLibRef
{
public:
	HMODULE hMod;
	CLibRef(const char* lib)
	{
		hMod = ::LoadLibrary(lib);
	}
	~CLibRef()
	{
		if(hMod) ::FreeLibrary(hMod);
	}
	virtual bool IsOK() const
	{
		return !!hMod;
	}

protected:
	void* GetProcAddress(const char* proc)
	{
		return ::GetProcAddress(hMod,proc);
	}
};

class CLibRef7Zip : public CLibRef
{
private:
	typedef UINT32  (__stdcall* CreateObjectFunc)(const GUID* guidin,const GUID* guidout,void** ppOut);
	typedef HRESULT (__stdcall* GetNumberOfFormatsFunc)(UINT32* pnumFormats);
	typedef HRESULT (__stdcall* GetHandlerProperty2Func)(UInt32 formatIndex, PROPID propID, PROPVARIANT* value);

	struct FormatRecord
	{
		std::vector<unsigned char> signature;
		GUID guid;
	};

private:
	bool m_bFail;
private:
	bool GetInterface()
	{
		CreateObject = (CreateObjectFunc)GetProcAddress("CreateObject");
		GetNumberOfFormats = (GetNumberOfFormatsFunc)GetProcAddress("GetNumberOfFormats");
		GetHandlerProperty2 = (GetHandlerProperty2Func)GetProcAddress("GetHandlerProperty2");

		return CreateObject && GetNumberOfFormats && GetHandlerProperty2;
	}
	bool GetFormatRecords()
	{
		UInt32 nFormats;
		GetNumberOfFormats(&nFormats);

		for(UInt32 i=0; i<nFormats; i++){
			PROPVARIANT prop;
			prop.vt = VT_EMPTY;

			GetHandlerProperty2(i,NArchive::kStartSignature,&prop);

			FormatRecord fr;
			UInt32 len;

			len = ::SysStringLen(prop.bstrVal);
			for(UInt32 x=0; x<len; x++){
				fr.signature.push_back(((unsigned char*)prop.bstrVal)[x]);
			}

			GetHandlerProperty2(i,NArchive::kClassID,&prop);
			memcpy(&fr.guid,prop.bstrVal,16);

			this->formatRecords.push_back(fr);

			::VariantClear(reinterpret_cast<VARIANTARG*>(&prop));
		}
		return true;
	}

public:
	CLibRef7Zip(const char* lib)
		: CLibRef(lib)
		, CreateObject(0)
		, GetNumberOfFormats(0)
		, GetHandlerProperty2(0)
		, m_bFail(true)
	{
		if(hMod){
			m_bFail = !( GetInterface() && GetFormatRecords());
		}
	}

	virtual bool IsOK() const override
	{
		return __super::IsOK() && !m_bFail;
	}

	bool bstr2string(BSTR bstr,std::string* result)
	{
		std::wstring tws = bstr;
		int buflen = (tws.size()+1)*2;
		char* buf = new char[buflen];
		int ret = ::WideCharToMultiByte(CP_ACP,0,tws.c_str(),tws.size(),buf,buflen,0,0);
		if(ret == 0){
			delete[] buf;
			return false;
		}

		buf[ret] = 0;
		*result = buf;
		delete[] buf;
		return true;
	}

public:
	CreateObjectFunc			CreateObject;
	GetNumberOfFormatsFunc		GetNumberOfFormats;
	GetHandlerProperty2Func		GetHandlerProperty2;

	std::vector<FormatRecord>	formatRecords;
};


bool GetArchiveHeaders(CLibRef7Zip& lib7z, std::vector<CNesFile>* files, const char* archive);

void GetDirectoryFiles(const char* dir, std::vector<std::string>* files);
