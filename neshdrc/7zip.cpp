#include "stdafx.h"

#include "7zip/IArchive.h"

#include "7zip.h"

#pragma comment(lib, "shlwapi.lib")

class CInMemStream : public IInStream, private IStreamGetSize
{
private:
	ULONG m_refCount;

private:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFGUID,void**)
	{
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef()
	{
		return ++m_refCount;
	}
	ULONG STDMETHODCALLTYPE Release()
	{
		return --m_refCount;
	}

public:
	HRESULT STDMETHODCALLTYPE GetSize(UInt64* size)
	{
		*size = m_Size;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Read(void* pdata,UInt32 length,UInt32* readLength)
	{
		if(m_curpos + length > m_Size-1){
			length = (m_Size-1) - m_curpos + 1;
		}

		if(length==0 || m_curpos>(long)m_Size-1){
			return E_INVALIDARG;
		}

		memcpy(pdata,(char*)m_pData+m_curpos,length);
		if(readLength) *readLength = length;

		m_curpos += length;

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Seek(Int64 offset,UInt32 origin,UInt64* pos)
	{
		long newpos;
		if(origin == SEEK_SET){
			newpos = 0 + (long)offset;
		}else if(origin == SEEK_CUR){
			newpos = m_curpos + (long)offset;
		}else if(origin == SEEK_END){
			newpos = (long)m_Size-1 + (long)offset;
		}

		if(0>newpos || newpos>(long)m_Size-1){
			return E_INVALIDARG;
		}

		m_curpos = newpos;
		if(pos) *pos=m_curpos;
		return S_OK;
	}


private:
	size_t		m_Size;
	const void*	m_pData;
	long		m_curpos;

public:
	CInMemStream(const void* pdata,size_t sz)
		: m_refCount(0)
		, m_Size(0)
		, m_pData(0)
		, m_curpos(0)
	{
		m_pData = pdata;
		m_curpos = 0;
		m_Size  = sz;
	}
	void rewind()
	{
		m_curpos = 0;
	}

	~CInMemStream()
	{
	}
};

class CExtractCallback : public IArchiveExtractCallback
{
private:
	class OutMemStream : public ISequentialOutStream
	{
	private:
		ULONG m_refCount;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFGUID,void**)
		{
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef()
		{
			return ++m_refCount;
		}

		ULONG STDMETHODCALLTYPE Release()
		{
			return --m_refCount;
		}

	private:
		HRESULT STDMETHODCALLTYPE Write(const void *data, UInt32 size, UInt32 *processedSize)
		{
			if(size > GetSpaceLeft()) ReallocateDataSpace(size);
			memcpy((char*)m_pData+GetSpaceUsed(),data,size);
			GetSpaceUsed() += size;
			*processedSize = size;
			return S_OK;
		}

	private:
		void*	m_pData;
		size_t	m_iDataSize;
		size_t	m_iDataPos;

	private:
		size_t GetGranularity()
		{
			return (size_t)1<<20;
		}
		size_t GetSpaceLeft()
		{
			//如果m_iDataPos == m_iDataSize
			//结果正确, 但貌似表达式错误?
			// -1 + 1 = 0, size_t 得不到-1, 0x~FF + 1,也得0
			return m_iDataSize-1 - m_iDataPos + 1;
		}
		size_t& GetSpaceUsed()
		{
			//结束-开始+1
			//return m_iDataPos-1 -0 + 1;
			return m_iDataPos;
		}

		void ReallocateDataSpace(size_t szAddition)
		{
			//只有此条件才会分配空间,也即调用此函数前必须检测空间剩余

			//计算除去剩余空间后还应增加的空间大小
			size_t left = szAddition - GetSpaceLeft();

			//按分配粒度计算块数及剩余
			size_t nBlocks = left / GetGranularity();
			size_t cbRemain = left - nBlocks*GetGranularity();
			if(cbRemain) ++nBlocks;

			//计算新空间并重新分配
			size_t newDataSize = m_iDataSize + nBlocks*GetGranularity();
			void*  pData = (void*)new char[newDataSize];

			//复制原来的数据到新空间
			memcpy(pData,m_pData,GetSpaceUsed());

			if(m_pData) delete[] m_pData;
			m_pData = pData;
			m_iDataSize = newDataSize;
		}

	public:
		OutMemStream():
			m_refCount(0),
			m_pData(0),
			m_iDataSize(0),
			m_iDataPos(0)
		{}
		~OutMemStream()
		{
			if(m_pData){
				delete[] m_pData;
				m_pData = 0;
			}
		}
		void* GetDataPtr()
		{
			return m_pData;
		}
		size_t GetDataSize()
		{
			return GetSpaceUsed();
		}
	};
private:
	ULONG m_refCount;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFGUID,void**)
	{
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef()
	{
		return ++m_refCount;
	}

	ULONG STDMETHODCALLTYPE Release()
	{
		return --m_refCount;
	}

private:
	HRESULT STDMETHODCALLTYPE PrepareOperation(Int32)
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetTotal(UInt64)
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*)
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetOperationResult(Int32)
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetStream(UInt32 id,ISequentialOutStream** ptr,Int32 mode)
	{
		switch (mode)
		{
		case NArchive::NExtract::NAskMode::kExtract:
		case NArchive::NExtract::NAskMode::kTest:

			if (/*id != m_index || */ptr == NULL)
				return S_FALSE;
			else
				*ptr = &m_OutStream;

		case NArchive::NExtract::NAskMode::kSkip:
			return S_OK;

		default:
			return E_INVALIDARG;
		}
	}

public:
	OutMemStream& GetStream(){return m_OutStream;}
private:
	OutMemStream m_OutStream;
	//UInt32		 m_index;

public:
	CExtractCallback():
		m_refCount(0)
	{

	}
};

bool GetArchiveHeaders(CLibRef7Zip& _7z, std::vector<CNesFile>* files, const void* data, size_t data_size, const char* fname)
{
	int matchingFormat = -1;
	for(int i=0; i<(int)_7z.formatRecords.size(); i++){
		int szsig = (int)_7z.formatRecords[i].signature.size();
		if(!szsig) continue;
		if( data_size >= szsig
			&& memcmp(&_7z.formatRecords[i].signature[0], data, szsig) == 0)
		{
			matchingFormat = i;
			break;
		}
	}

	if(matchingFormat == -1){
		delete[] data;
		return false;
	}

	IInArchive* farch = nullptr;
	try{
		if(FAILED(_7z.CreateObject(&_7z.formatRecords[matchingFormat].guid, &IID_IInArchive, (void**)&farch)))
			throw "7zip无效!";

		CInMemStream ims(data, data_size);
		ims.rewind();
		if(FAILED(farch->Open(&ims, 0, 0)))
			throw "7zip无法打开此档案文件!";

		UInt32 nFiles;
		if(FAILED(farch->GetNumberOfItems(&nFiles)))
			throw "7zip无法取得文件数目!";

		for(UInt32 i=0; i<nFiles; i++){
			PROPVARIANT prop;
			prop.vt = VT_EMPTY;

			if(FAILED(farch->GetProperty(i, kpidIsFolder, &prop)))
				throw "7zip无法取得属性值!";

			if(prop.boolVal == VARIANT_TRUE)
				continue;

			if(FAILED(farch->GetProperty(i, kpidPath, &prop)))
				throw "7zip无法取得文件名!";

			std::string fn;
			_7z.bstr2string(prop.bstrVal, &fn);

			auto CheckExts = [](const char* fname)->int{
				const char* exts[] = {".nes",".7z",".zip",".rar", nullptr};
				int sz = sizeof(exts)/sizeof(exts[0]);
				for(int i=0; i<sz; i++){
					auto p = StrStrI(fname, exts[i]);
					if(p){
						p += strlen(exts[i]);
						if(!*p) return i;
					}
				}
				return -1;
			};

			auto exti = CheckExts(fn.c_str());
			if(exti == -1){
				continue;
			}

			// nes or archive
			CExtractCallback extract;
			UInt32 indices[1] = {i,};
			if(FAILED(farch->Extract(&indices[0], 1, 0, &extract)))
				throw "7zip提取文件时出错!";

			if(exti == 0){ // nes
				CNesFile f;
				f.fname = fname;
				f.fname += " -> ";
				f.fname += fn.c_str();
				if(extract.GetStream().GetDataSize() >= 16){
					memcpy(f.hdr, extract.GetStream().GetDataPtr(),16);
				}
				else{
					memset(f.hdr, 0, 16);
				}
				files->push_back(f);
			}
			else{ // archiver
				std::string tmp(fname);
				tmp += " -> ";
				tmp += fn.c_str();
				GetArchiveHeaders(_7z, files, extract.GetStream().GetDataPtr(), extract.GetStream().GetDataSize(), tmp.c_str());
			}
			VariantClear(reinterpret_cast<VARIANTARG*>(&prop));
		}
	}
	catch(const char* str)
	{
		if(farch) farch->Release();
		return false;
	}
	return true;
}

bool GetArchiveHeaders(CLibRef7Zip& lib7z, std::vector<CNesFile>* files, const char* archive)
{
	FILE* fp = fopen(archive, "rb");
	if(!fp){
		return false;
	}

	unsigned char* data = nullptr;
	size_t data_size;

	fseek(fp, 0, SEEK_END);
	data_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	data = new unsigned char[data_size];
	if(fread(data, 1, data_size, fp) != data_size){
		fclose(fp);
		delete[] data;
		return false;
	}
	fclose(fp);
	fp = nullptr;

	if(data_size >= 16){
		if(memcmp(data, "NES\x1A",4) == 0){
			CNesFile f;
			memcpy(f.hdr, data, 16);
			f.fname = archive;
			files->push_back(f);
			delete[] data;
			return true;
		}
	}

	bool r = GetArchiveHeaders(lib7z, files, data, data_size, archive);
	delete[] data;
	return r;
}


void GetDirectoryFiles(const char* dir, std::vector<std::string>* files)
{
	WIN32_FIND_DATA fd;
	HANDLE hFile = INVALID_HANDLE_VALUE;

	std::string sdir(dir);
	if(sdir[sdir.size()-1] != '\\'
		&& sdir[sdir.size()-1] != '/')
	{
		sdir += '/';
	}

	const char* exts[] = {"*.nes", "*.7z", "*.rar", "*.zip"};
	for(auto ext : exts){
		std::string find(sdir);
		find += ext;
		hFile = FindFirstFile(find.c_str(), &fd);
		if(hFile != INVALID_HANDLE_VALUE){
			do{
				std::string f(sdir);
				f += fd.cFileName;
				if(GetFileAttributes(f.c_str()) & FILE_ATTRIBUTE_DIRECTORY){
					GetDirectoryFiles(f.c_str(), files);
				}
				else{
					files->push_back(f);
				}
			}while(FindNextFile(hFile, &fd));
			FindClose(hFile);
		}
	}
}
