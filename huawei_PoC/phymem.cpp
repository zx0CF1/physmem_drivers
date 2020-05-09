typedef unsigned long ulong_t;

#include <windows.h>
#include <winternl.h>
#include <process.h>
#include <wtsapi32.h>
#include <tlhelp32.h>
#include <conio.h>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <immintrin.h>

#define MAP_CONTROL_CODE 0x80102040
#define UNMAP_CONTROL_CODE 0x80102044

struct control_io_t
{
	uint64_t m_size;
	uint64_t m_physical_address;
	uint64_t m_section_handle;
	uint64_t m_user_address;
	uint64_t m_section_object;
};

class c_kernel_memory
{
public:
	c_kernel_memory()
	{
		m_device_handle = CreateFileW(L"\\\\.\\PhyMem", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		std::cout << std::hex << m_device_handle << std::endl;
		m_directory_base = find_directory_base();
		std::cout << std::hex << m_directory_base << std::endl;
	}

	__forceinline const uint64_t find_directory_base()
	{
		uint8_t* buffer = new uint8_t[0x10000];

		for (int i = 0; i < 10; i++)
		{
			if (!read_write_physical_address(static_cast<uint64_t>(i) * 0x10000, buffer, 0x10000)) continue;

			for (int offset = 0; offset < 0x10000; offset += 0x1000)
			{
				if ((0x00000001000600E9 ^ (0xffffffffffff00ff & *(uint64_t*)(buffer + offset))) ||
					(0xfffff80000000000 ^ (0xfffff80000000000 & *(uint64_t*)(buffer + offset + 0x70))) ||
					(0xffffff0000000fff & *(uint64_t*)(buffer + offset + 0xA0)))
					continue;

				uint64_t directory_base = *(uint64_t*)(buffer + offset + 0xA0);
				delete[] buffer;

				return directory_base;
			}
		}

		delete[] buffer;
		return 0;
	}

	const uint64_t convert_virtual_to_physical(const uint64_t virtual_address)
	{
		if (!m_directory_base) return 0;

		//read PML4E
		uint64_t PML4E = 0;
		uint16_t PML4 = (uint16_t)((virtual_address >> 39) & 0x1FF);
		read_write_physical_address(m_directory_base + (PML4 * 8), &PML4E, 8);
		if (!PML4E) return 0;

		//read PDPTE 
		uint64_t PDPTE = 0;
		uint16_t DirPtr = (uint16_t)((virtual_address >> 30) & 0x1FF);
		read_write_physical_address((PML4E & 0xFFFFFFFFFF000) + (DirPtr * 8), &PDPTE, 8);
		if (!PDPTE) return 0;

		//PS=1 (1GB page)
		if ((PDPTE & (1 << 7)) != 0)
		{
			//if (PageSize) *PageSize = 0x40000000/*1Gb*/;
			return (PDPTE & 0xFFFFFC0000000) + (virtual_address & 0x3FFFFFFF);
		}

		//read PDE 
		uint64_t PDE = 0;
		uint16_t Dir = (uint16_t)((virtual_address >> 21) & 0x1FF);
		read_write_physical_address((PDPTE & 0xFFFFFFFFFF000) + (Dir * 8), &PDE, 8);
		if (!PDE) return 0;

		//PS=1 (2MB page)
		if ((PDE & (1 << 7)) != 0)
		{
			//if (PageSize) *PageSize = 0x200000/*2MB*/;
			return (PDE & 0xFFFFFFFE00000) + (virtual_address & 0x1FFFFF);
		}

		//read PTE
		uint64_t PTE = 0;
		uint16_t Table = (uint16_t)((virtual_address >> 12) & 0x1FF);
		read_write_physical_address((PDE & 0xFFFFFFFFFF000) + (Table * 8), &PTE, 8);
		if (!PTE) return 0;

		//BasePage (4KB Page)
		//if (PageSize) *PageSize = 0x1000/*4KB*/;
		return (PTE & 0xFFFFFFFFFF000) + (virtual_address & 0xFFF);
	}

	template<class t>
	__forceinline bool read_write_physical_address(const uint64_t physical_address, t* buffer, const uint64_t size, bool read = true)
	{
		ulong_t returned_size;
		control_io_t* control_io = new control_io_t();

		control_io->m_physical_address = physical_address;
		control_io->m_size = size;

		if (!DeviceIoControl(m_device_handle, MAP_CONTROL_CODE, control_io, sizeof(control_io_t), control_io, sizeof(control_io_t), &returned_size, 0))
		{
			delete control_io;
			return false;
		}

		if (control_io->m_user_address)
		{
			memcpy
			(
				read ? reinterpret_cast<void*>(buffer) : reinterpret_cast<void*>(control_io->m_user_address),
				read ? reinterpret_cast<void*>(control_io->m_user_address) : reinterpret_cast<void*>(buffer),
				size
			);

			DeviceIoControl(m_device_handle, UNMAP_CONTROL_CODE, control_io, sizeof(control_io_t), control_io, sizeof(control_io_t), &returned_size, 0);
			delete control_io;
			return true;
		}
		else
		{
			delete control_io;
			return false;
		}
	}

private:
	HANDLE m_device_handle;
	uint64_t m_directory_base;
};