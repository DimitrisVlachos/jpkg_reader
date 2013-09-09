/*
	Author : Dimitris Vlachos (DimitrisV22@gmail.com @ github.com/DimitrisVlachos)

	Filesystem reader implementation.
	Supports both my package format & reading off a naked package directory(for quick prototyping).
	Stream readers are passed as template arguments to maximize the file system's wrapping capabilities(ie 
	reading off dvd/cd/bluray/ram-fs/embedded media).
*/

#ifndef __filesystem__hpp__
#define __filesystem__hpp__
#include "file_stream.hpp" /*Grab it from my lib_bitstreams repo : https://github.com/DimitrisVlachos/lib_bitstreams */
#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace file_system {
	namespace private_section {
		 static int32_t decompress_from_stream_to_mem(file_streams::file_stream_if* source,uint8_t* dst,uint32_t max_len,uint32_t chunk_size,uint8_t* chunk);
	}

struct file_system_entry_t {
	std::string name;
	uint64_t addr;
	uint64_t size;

	file_system_entry_t() {}
	file_system_entry_t(const std::string& in_name,const uint64_t in_addr,const uint64_t in_size) :
	name(in_name),addr(in_addr),size(in_size) {
	}
};

class file_system_if {
	private:
	public:
	virtual ~file_system_if() {}
	virtual bool open(const std::string& path) { return false; }
	virtual void close() { }
	//Find an entry in the package
	virtual file_system_entry_t* find_entry(const std::string& name) { return 0; }
	//Read whole entry to allocated buffer out_data
	virtual bool read_entry(file_system_entry_t* entry,uint8_t* out_data) { return false; }
};

/*Dummy reader - reads contents off a directory (for quick prototyping)*/
template <class reader_type_c>
class file_system_reader_c : public file_system_if {
	private:
	std::vector<file_system_entry_t> m_entries;
	
	file_system_entry_t* register_entry(const std::string& name) {
		file_system_entry_t tmp;
		file_streams::file_stream_if* reader;

		for (uint32_t i = 0U,j = m_entries.size();i < j;++i) {
			if (m_entries[i].name == name) 
				return &m_entries[i];
		}
 
		reader = new reader_type_c( name.c_str());
		if (reader) {
			m_entries.push_back(file_system_entry_t(name,0,reader->size()));
			tmp.size = reader->size();
			delete reader;

			return &m_entries.back();
		}  
		
		return 0;//Error!		
	}

	public:
	file_system_reader_c() {}
	~file_system_reader_c() {  close(); }

	bool open(const std::string& path) { return true; }
	void close() { }
 

	bool read_entry(file_system_entry_t* entry,uint8_t* out_data) {
		file_streams::file_stream_if* reader;
	
		if (!entry)
			return false;

		reader = new reader_type_c(entry->name.c_str());
		if (!reader)
			return false;

		reader->read(out_data,entry->size);
		delete reader;
		return true;
	}

	file_system_entry_t* find_entry(const std::string& name) {	
		return register_entry(name);
	}
};

/*Package reader - Supports my (read-only) package format*/
template <class reader_type_c>
class file_system_pkg_reader_c : public file_system_if {
	private:
	std::vector<file_system_entry_t> m_entries;
	file_streams::file_stream_if* m_reader;
	uint8_t* m_dcmp_chunk;

	inline uint64_t decode64(file_streams::file_stream_if* rd) {
		register uint8_t tmp[8];
		register uint64_t res;

		if (rd->eof())
			return 0;

		rd->read(tmp,8);

		res = (uint64_t)tmp[0] << (uint64_t)56U;
		res |= (uint64_t)tmp[1] << (uint64_t)48U;
		res |= (uint64_t)tmp[2] << (uint64_t)40U;
		res |= (uint64_t)tmp[3] << (uint64_t)32U;
		res |= (uint64_t)tmp[4] << (uint64_t)24U;
		res |= (uint64_t)tmp[0] << (uint64_t)16U;
		res |= (uint64_t)tmp[6] << (uint64_t)8U;
		res |= (uint64_t)tmp[7];

		return res;
	}

	inline std::string decode_string(file_streams::file_stream_if* rd) {
		uint8_t c;
		std::string res;

		for (uint64_t i = rd->tell(),j = rd->size();i < j;++i) {
			if (rd->eof())
				return res;

			c = rd->read();
			if (0 == c)
				return res;

			res += (char)c;
		}
		return res;
	}

	bool read_hdr() {
		const std::string cs_signature = "JVFS0100";

		if (!m_reader)
			return false;

		m_reader->seek(0);
		{
			std::string hdr;
			hdr = decode_string(m_reader);
			if (hdr != cs_signature)
				return false;
			if (m_reader->eof())
				return false;
		}

		const uint64_t entry_cnt = decode64(m_reader),lim = m_reader->size();
		if (m_reader->eof())
			return false;

		m_entries.clear();
		m_entries.reserve(entry_cnt);

		//printf("%llu entries in pkg\n",entry_cnt);
		for (uint64_t i = 0U;i < entry_cnt;++i) {
			const uint64_t addr = decode64(m_reader); 

			if (m_reader->eof())
				return false;

			if (addr > lim)
				return false;

			const uint64_t size = decode64(m_reader);

			if (m_reader->eof())
				return false;

			const std::string name = decode_string(m_reader);

			m_entries.push_back(file_system_entry_t(name,addr,size));

			//printf("add %s %llu %llu/%llu\n",name.c_str(),addr,size,lim);
		}

		return !m_entries.empty();
	}

	public:
	file_system_pkg_reader_c() : m_reader(0),m_dcmp_chunk(0) { m_dcmp_chunk = new uint8_t[16*1024*2];}
	~file_system_pkg_reader_c() { close(); delete[] m_dcmp_chunk; }
 
	bool read_entry(file_system_entry_t* entry,uint8_t* out_data) {
		if (!entry || !m_reader)
			return false;
 
		m_reader->seek(entry->addr);
		return private_section::decompress_from_stream_to_mem(m_reader,out_data,entry->size,16*1024,m_dcmp_chunk) == Z_OK;
	}

	file_system_entry_t* find_entry(const std::string& name) {	
		for (uint32_t i = 0U,j = m_entries.size();i < j;++i) {
			if (m_entries[i].name == name) 
				return &m_entries[i];
		}
		return 0;
	}

	void close() {
		if (m_reader) {
			delete m_reader;		
			m_reader = 0;
		}
		m_entries.clear();
	}

	bool open(const std::string& path) {
		close();
		if (!m_dcmp_chunk)
			return false;
		m_reader = new reader_type_c(path.c_str());
		if (!m_reader)
			return false;
		else if (!m_reader->is_open()) {
			close();
			return false;
		}

		if (!read_hdr()) {
			close();
			return false;
		}
		return true;
	}
};

	namespace private_section {
		 static int32_t decompress_from_stream_to_mem(file_streams::file_stream_if* source,uint8_t* dst,uint32_t max_len,uint32_t chunk_size,uint8_t* chunk) {
			int32_t ret;
			uint32_t have;
			z_stream strm;
			uint8_t* in = chunk;
			uint8_t* end = dst + max_len;
		 
			/* allocate inflate state */
			strm.zalloc = Z_NULL;
			strm.zfree = Z_NULL;
			strm.opaque = Z_NULL;
			strm.avail_in = 0;
			strm.next_in = Z_NULL;
			ret = inflateInit(&strm);
			if (ret != Z_OK)
				return ret;

			do {
				strm.avail_in = source->read(in, chunk_size );

				if (strm.avail_in == 0) {
					ret = Z_STREAM_END ;
				    break;
				}

				strm.next_in = in;

				do {
				    strm.avail_out = chunk_size;
				    strm.next_out = dst;
				    ret = inflate(&strm, Z_NO_FLUSH);
				    switch (ret) {
						case Z_NEED_DICT:  
						    ret = Z_DATA_ERROR;
						case Z_DATA_ERROR:   
						case Z_MEM_ERROR:   
						    inflateEnd(&strm);
						    return ret;
				    }
				    have = chunk_size - strm.avail_out; 
				    if ((dst + have) > end) {
				        inflateEnd(&strm);
				        return Z_OK;
				    }
					dst += have;
				} while (strm.avail_out == 0);
			} while (ret != Z_STREAM_END);

			inflateEnd(&strm);
			return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
		}
	}
}
#endif


