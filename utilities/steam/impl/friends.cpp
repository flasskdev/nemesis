#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../steam.hpp"

namespace steam {
    namespace detail { inline std::uintptr_t friends_interface{}; }

    bool friends::initialize() {
        if (!detail::friends_interface)
            detail::friends_interface = memory::call<std::uintptr_t>(MODULE_EXPORT("steam_api64.dll:SteamAPI_SteamFriends_v018"));
        return detail::friends_interface != 0;
    }

    const char* friends::get_persona_name() {
        if (!initialize()) return nullptr;
        return memory::call<const char*>(MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamFriends_GetPersonaName"), detail::friends_interface);
    }

    constexpr std::uint64_t k_steam_id_base = 76561197960265728ull;

    int friends::get_medium_friend_avatar(std::uint64_t steam_id) {
        if (steam_id < k_steam_id_base || !initialize()) return 0;
        return memory::call<int>(MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamFriends_GetMediumFriendAvatar"), detail::friends_interface, steam_id);
    }

    bool friends::request_user_information(std::uint64_t steam_id, bool name_only) {
        if (steam_id < k_steam_id_base || !initialize()) return false;
        return memory::call<bool>(MODULE_EXPORT("steam_api64.dll:SteamAPI_ISteamFriends_RequestUserInformation"), detail::friends_interface, steam_id, name_only);
    }
}
