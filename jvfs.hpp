/*
	Author : Dimitris Vlachos (DimitrisV22@gmail.com @ github.com/DimitrisVlachos)

	Filesystem reader implementation.
	Supports both my package format & reading off a naked package directory(for quick prototyping).
	Stream readers are passed as template arguments to maximize the file system's wrapping capabilities(ie 
	reading off dvd/cd/bluray/ram-fs/embedded media).
*/

#ifndef __jvfs__hpp__
#define __jvfs__hpp__
#include "file_stream.hpp" /*Grab it from my lib_bitstreams repo : https://github.com/DimitrisVlachos/lib_bitstreams */
#include <iostream>
#include <cstring>
#include <string>
#include <map>
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
	std::string m_path;
	std::string m_sep;
	std::map<std::string,file_system_entry_t> m_entries;

	inline const std::string invert_sep() const { return (m_sep == "/") ? "\\" : "/"; }

	file_system_entry_t* register_entry(const std::string& name) {
		file_streams::file_stream_if* reader;
 		std::string fpath;
		std::pair<std::map<std::string,file_system_entry_t>::iterator,bool> it;
		std::map<std::string,file_system_entry_t>::iterator it2;

		fpath = m_path + name;
		{
			const char inv = invert_sep()[0];
			for (uint32_t i = 0,j = fpath.length();i < j;++i) {
				if (fpath[i] == inv)
					fpath[i] = m_sep[0];
			}
		}

		it = m_entries.insert( std::pair<std::string,file_system_entry_t>(name,file_system_entry_t(fpath,0,0)) );

		it2 = it.first;
		if (false == it.second)
			return &it2->second;

		reader = new reader_type_c( fpath.c_str() );
		if (reader) {
			it2->second.size = reader->size();
			delete reader;
			return  &it2->second;
		}  
		
		return 0;//Error!		
	}

	public:
	file_system_reader_c() : m_path(""), m_sep("/")  {}
	~file_system_reader_c() {  close(); }

	void set_path_separator(const std::string& sep) {
		m_sep = sep;
	}

	bool open(const std::string& path) { 
		m_path = path; 
		if (!m_path.empty()) {
			if (!m_sep.empty()) {
				if (m_sep[0] != m_path[m_path.length()-1])
					m_path += m_sep[0];
			}
		}
		return true; 
	}

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
	std::map<std::string,file_system_entry_t> m_entries;
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

	/*0101 format : Supports compressed headers*/
	bool read_hdr0101() {	
		
		if (m_reader->eof())
			return false;

		const uint64_t hdr_offs = decode64(m_reader);
		if (m_reader->eof())
			return false;

		if (hdr_offs >= m_reader->size())
			return false;

		m_reader->seek(hdr_offs);

		const uint64_t hdr_block_sz = decode64(m_reader); 
		if (m_reader->eof())
			return false;

		uint8_t* hdr = new uint8_t[hdr_block_sz];
		if (!hdr)
			return false;

		if (private_section::decompress_from_stream_to_mem(m_reader,hdr,hdr_block_sz,16*1024,m_dcmp_chunk) != Z_OK) {
			delete[] hdr;
			return false;
		}

		file_streams::file_stream_if* unc_hdr_rd = new file_streams::file_mem_reader_c(&hdr[0],hdr_block_sz,true);
		if (!unc_hdr_rd) {
			delete[] hdr;
			return false;
		}

		const uint64_t entry_cnt = decode64(unc_hdr_rd),lim = m_reader->size() ;
 

		m_entries.clear();

		for (uint64_t i = 0U;i < entry_cnt;++i) {
			const uint64_t addr = decode64(unc_hdr_rd); 

			if (unc_hdr_rd->eof()) {
				delete unc_hdr_rd;  //will delete[] shared hdr
				return false;
			}

			if (addr > lim) {
				delete unc_hdr_rd;  //will delete[] shared hdr
				return false;
			}

			const uint64_t size = decode64(unc_hdr_rd);

			if (unc_hdr_rd->eof()) {
				delete unc_hdr_rd;  //will delete[] shared hdr
				return false;
			}

			const std::string name = decode_string(unc_hdr_rd);

			m_entries.insert( std::pair<std::string,file_system_entry_t>(name,file_system_entry_t(name,addr,size)));
		}
		
		delete unc_hdr_rd; //will delete[] shared hdr
		return !m_entries.empty();
	}

	bool read_hdr() {
		const std::string cs_signature = "JVFS0100";
		const std::string cs_signature_v1 = "JVFS0101";

		if (!m_reader)
			return false;

		m_reader->seek(0);
		{
			std::string hdr;
			hdr = decode_string(m_reader);
			if (hdr != cs_signature) {
				if (hdr == cs_signature_v1)
					return read_hdr0101();
				return false;
			}
			if (m_reader->eof())
				return false;
		}

		const uint64_t entry_cnt = decode64(m_reader),lim = m_reader->size();
		if (m_reader->eof())
			return false;

		m_entries.clear();

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

			m_entries.insert( std::pair<std::string,file_system_entry_t>(name,file_system_entry_t(name,addr,size)));
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
		std::map<std::string,file_system_entry_t>::iterator it;

		it = m_entries.find(name);
		if (it != m_entries.end())
			return &it->second;
		return 0;
	}
	
	void set_path_separator(const std::string& sep) {}

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
