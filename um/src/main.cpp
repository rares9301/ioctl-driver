#include <iostream>

#include <Windows.h>
#include <Tlhelp32.h>
#include <cmath>

#include "client.dll.hpp"
#include "offsets.hpp"

static DWORD get_process_id(const wchar_t* process_name) {
	DWORD process_id = 0;

	HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (snap_shot == INVALID_HANDLE_VALUE)
		return process_id;

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(decltype(entry));

	if (Process32FirstW(snap_shot, &entry) == TRUE) {
		// Check if the first handle is the one we want
		if (_wcsicmp(process_name, entry.szExeFile) == 0)
			process_id = entry.th32ProcessID;
		else {
			while (Process32NextW(snap_shot, &entry) == TRUE) {
				if (_wcsicmp(process_name, entry.szExeFile) == 0) {
					process_id = entry.th32ProcessID;
					break;
				}
			}
		}
	}

	CloseHandle(snap_shot);

	return process_id;

}

static std::uintptr_t get_module_base(const DWORD pid, const wchar_t* module_name) {
	std::uintptr_t module_base = 0;

	// Snap-shot of process modules (dlls)
	HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snap_shot == INVALID_HANDLE_VALUE)
		return module_base;

	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(decltype(entry));

	if (Module32FirstW(snap_shot, &entry) == TRUE) {
		if (wcsstr(module_name, entry.szModule) != nullptr)
			module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
		else {
			while (Module32NextW(snap_shot, &entry) == TRUE) {
				if (wcsstr(module_name, entry.szModule) != nullptr) {
					module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
					break;
				}
			}
		}
	}

	CloseHandle(snap_shot);

	return module_base;
}

struct Vector3 {
	float x, y, z;
};

float calculate_distance(const Vector3& point1, const Vector3& point2) {
	const float dx = point1.x - point2.x;
	const float dy = point1.y - point2.y;
	const float dz = point1.z - point2.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Funcție pentru a calcula unghiul necesar pentru a îndrepta arma către capul inamicului
Vector3 calculate_aim_angle(const Vector3& view_angles, const Vector3& enemy_head_position) {
	Vector3 aim_angle;

	// Implementează logica de calcul a unghiului aici
	// Poți folosi coordonatele jucătorului și ale inamicului pentru a determina unghiul dorit.

	// Exemplu simplu: Acesta va îndrepta arma către poziția capului inamicului
	aim_angle.x = enemy_head_position.x - view_angles.x;
	aim_angle.y = enemy_head_position.y - view_angles.y;
	aim_angle.z = enemy_head_position.z - view_angles.z;

	return aim_angle;
}

namespace driver {
	namespace codes {
		// used for driver setup
		constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

		// read process memory
		constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

		// write process memory
		constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	} // namespace codes

	// Shared between user mode & kernel mode
	struct Request {
		HANDLE	process_id;

		PVOID target;
		PVOID buffer;

		SIZE_T size;
		SIZE_T return_size;
	};

	bool attach_to_process(HANDLE driver_handle, const DWORD pid) {
		Request r;
		r.process_id = reinterpret_cast<HANDLE>(pid);
		
		return DeviceIoControl(driver_handle, codes::attach, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr); 
	}

	template <class T>
	T read_memory(HANDLE driver_handle, const std::uintptr_t addr) {
		T temp = {};

		Request r; 
		r.target = reinterpret_cast<PVOID>(addr);
		r.buffer = &temp; 
		r.size = sizeof(T);

		DeviceIoControl(driver_handle, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);

		return temp;
	}

	template <class T>
	void write_memory(HANDLE driver_handle, const std::uintptr_t addr, const T& value) {
		Request r;
		r.target = reinterpret_cast<PVOID>(addr);
		r.buffer = (PVOID)&value;
		r.size = sizeof(T);

		DeviceIoControl(driver_handle, codes::write, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
	}

	void activate_aim_bot(HANDLE driver_handle, const std::uintptr_t client_base) {
		// Obține adresa jucătorului local (tu)
		const auto local_player_pawn = read_memory<std::uintptr_t>(driver_handle, client_base + client_dll::dwLocalPlayerPawn);

		if (local_player_pawn == 0)
			return; // Nu putem continua fără o adresă validă pentru jucătorul local

		// Obține coordonatele tale (jucătorul local)
		const auto local_position = read_memory<Vector3>(driver_handle, local_player_pawn + client_dll::dwPlantedC4);

		// Obține adresa entității inamice cea mai apropiată
		std::uintptr_t closest_enemy = 0;
		float closest_distance = std::numeric_limits<float>::max();

		for (int i = 1; i <= 64; ++i) {
			const auto enemy_pawn = read_memory<std::uintptr_t>(driver_handle, client_base + client_dll::dwEntityList + i * 0x10);
			if (enemy_pawn != 0) {
				const auto enemy_position = read_memory<Vector3>(driver_handle, enemy_pawn + client_dll::dwPlantedC4);
				const float distance = calculate_distance(local_position, enemy_position);
				if (distance < closest_distance) {
					closest_distance = distance;
					closest_enemy = enemy_pawn;
				}
			}
		}

		// Calculează unghiul necesar pentru a îndrepta arma către inamicul cel mai apropiat
		const auto view_angles = read_memory<Vector3>(driver_handle, client_base + client_dll::dwViewAngles);
		const auto aim_angle = calculate_aim_angle(view_angles, closest_enemy);

		// Setează unghiul de vizualizare al jucătorului
		write_memory(driver_handle, client_base + client_dll::dwViewAngles, aim_angle);
	}
} // namespace driver

int main() {
	const DWORD pid = get_process_id(L"cs2.exe");

	if (pid == 0) {
		std::cout << "Failed to find cs2\n";
		std::cin.get();
		return 1;
	}

	const HANDLE driver = CreateFile(L"\\\\.\\ioctldriver", GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (driver == INVALID_HANDLE_VALUE) {
		std::cout << "Failed to create driver handle.\n";
		std::cin.get();
		return 1;
	}

	if (driver::attach_to_process(driver, pid) == true) {
		std::cout << "Attachment successful.\n";

		if (const std::uintptr_t client = get_module_base(pid, L"client.dll"); client != 0) {
			std::cout << "Client found.\n";

			while (true) {
				if (GetAsyncKeyState(VK_END))
					break;

				const auto local_player_pawn = driver::read_memory<std::uintptr_t>(driver, client + client_dll::dwLocalPlayerPawn);

				if (local_player_pawn == 0)
					continue;

				const auto flags = driver::read_memory<std::uint32_t>(driver, local_player_pawn + C_BaseEntity::m_fFlags);

				const bool in_air = flags & (1 << 0);
				const bool space_pressed = GetAsyncKeyState(VK_SPACE);
				const auto force_jump = driver::read_memory<DWORD>(driver, client + client_dll::dwForceJump);

				if (space_pressed && in_air) {
					Sleep(5);
					driver::write_memory(driver, client + client_dll::dwForceJump, 65537);
				} 
				else if (space_pressed && !in_air) {
					driver::write_memory(driver, client + client_dll::dwForceJump, 256);
				}
				else if (!space_pressed && force_jump == 65537) {
					driver::write_memory(driver, client + client_dll::dwForceJump, 256);
				}
			
				if (GetAsyncKeyState(VK_F1)) {
					driver::activate_aim_bot(driver, client);
				}
			}
		}
	}
		

	CloseHandle(driver);

	std::cin.get();

	return 0;
}