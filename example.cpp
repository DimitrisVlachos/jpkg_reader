#include "filesystem.hpp"

int main() {
	file_system::file_system_if* fs = new file_system::file_system_pkg_reader_c<file_streams::file_stream_reader_c>();
	file_system::file_system_entry_t* ent;
	uint8_t* blk;

	fs->open("out.pkg");

	ent = fs->find_entry("pkg/hello.txt");
	if (!ent) {
		printf("entry not found\n");	
		delete fs;
		return 0;
	}

	blk = new uint8_t[ent->size];
	if (!blk) {
		printf("out of mem!\n");
		delete fs;
		return 0;
	}

	fs->read_entry(ent, blk);
	printf("%u %u\n",ent->addr,ent->size);
	for (uint64_t i = 0;i < ent->size;++i)
		printf("0x%02x ", blk[i]);

	printf("\n");
	delete[] blk;
	delete fs;
	return 0;
}


