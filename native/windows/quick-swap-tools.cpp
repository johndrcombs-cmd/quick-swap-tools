#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cwctype>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "vendor/nlohmann-json.hpp"

namespace {
struct Hotkey {
    UINT modifiers = 0;
    UINT virtualKey = 0;
};

bool operator==(const Hotkey &left, const Hotkey &right) {
    return left.modifiers == right.modifiers
        && left.virtualKey == right.virtualKey;
}

struct HotkeyPair {
    Hotkey auction;
    Hotkey giveaway;
};

bool operator==(const HotkeyPair &left, const HotkeyPair &right) {
    return left.auction == right.auction && left.giveaway == right.giveaway;
}

struct HotkeyTransactionBackend {
    std::function<bool()> clear;
    std::function<bool(const Hotkey &)> writeAuction;
    std::function<bool(const Hotkey &)> writeGiveaway;
    std::function<std::optional<HotkeyPair>()> read;
};

bool commitHotkeyPair(
    const HotkeyPair &desired,
    const HotkeyTransactionBackend &backend) {
    if (!backend.clear()
        || !backend.writeAuction(desired.auction)
        || !backend.writeGiveaway(desired.giveaway)) {
        return false;
    }
    const auto saved = backend.read();
    return saved && *saved == desired;
}

struct TransactionResult {
    bool succeeded = false;
    bool rollbackRestored = false;
};

TransactionResult applyHotkeyPair(
    const HotkeyPair &original,
    const HotkeyPair &desired,
    const HotkeyTransactionBackend &backend) {
    if (commitHotkeyPair(desired, backend)) {
        return {true, true};
    }
    const bool restored = commitHotkeyPair(original, backend);
    return {false, restored};
}

constexpr std::uint32_t kMaxMessageBytes = 1024U * 1024U;
constexpr UINT kAuctionHotkeyId = 1;
constexpr UINT kGiveawayHotkeyId = 2;
constexpr UINT kInputClosedMessage = WM_APP + 1;
constexpr UINT kReloadHotkeysMessage = WM_APP + 2;
constexpr auto kBounceInterval = std::chrono::milliseconds(150);
constexpr wchar_t kSettingsRegistryPath[] = L"Software\\OniByts\\Quick Swap Tools";
constexpr wchar_t kReloadEventName[] = L"Local\\OniByts.QuickSwapTools.Reload";
constexpr wchar_t kReloadOkEventName[] = L"Local\\OniByts.QuickSwapTools.ReloadOk";
constexpr wchar_t kReloadFailedEventName[] = L"Local\\OniByts.QuickSwapTools.ReloadFailed";
constexpr wchar_t kStopEventName[] = L"Local\\OniByts.QuickSwapTools.Stop";
constexpr wchar_t kHotkeyOwnerMutexName[] = L"Local\\OniByts.QuickSwapTools.HotkeyOwner.v1";
constexpr wchar_t kConfiguratorMutexName[] = L"Local\\OniByts.QuickSwapTools.Configurator.v1";
constexpr wchar_t kLogitechR400DeviceId[] = L"VID_046D&PID_C538&MI_00";

enum class PhysicalAction {
    none,
    auction,
    giveaway,
};

bool containsCaseInsensitive(std::wstring_view text, std::wstring_view expected) {
    return std::search(
               text.begin(),
               text.end(),
               expected.begin(),
               expected.end(),
               [](wchar_t left, wchar_t right) {
                   return std::towupper(left) == std::towupper(right);
               }) != text.end();
}

PhysicalAction logitechR400Action(std::wstring_view deviceName, UINT virtualKey) {
    if (!containsCaseInsensitive(deviceName, kLogitechR400DeviceId)) {
        return PhysicalAction::none;
    }
    if (virtualKey == VK_PRIOR) {
        return PhysicalAction::auction;
    }
    if (virtualKey == VK_NEXT) {
        return PhysicalAction::giveaway;
    }
    return PhysicalAction::none;
}

bool isRawKeyboardPacketSizeValid(UINT size) {
    constexpr std::size_t minimum = offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD);
    return size >= minimum && size <= 64U * 1024U;
}

bool isLogitechR400ReservedHotkey(UINT virtualKey) {
    return virtualKey == VK_PRIOR || virtualKey == VK_NEXT;
}

bool isValidHotkey(const Hotkey &hotkey);

const char *physicalActionName(PhysicalAction action) {
    switch (action) {
    case PhysicalAction::auction:
        return "auction";
    case PhysicalAction::giveaway:
        return "giveaway";
    default:
        return "";
    }
}

int runLogitechR400ProfileSelfTest() {
    constexpr std::wstring_view r400 =
        LR"(\\?\hid#vid_046d&pid_c538&mi_00#test)";
    const PhysicalAction previous = logitechR400Action(r400, VK_PRIOR);
    const PhysicalAction next = logitechR400Action(r400, VK_NEXT);
    const bool matchedDevice = previous == PhysicalAction::auction
        && next == PhysicalAction::giveaway;
    const bool otherDeviceIgnored = logitechR400Action(
        LR"(\\?\HID#VID_1B1C&PID_1BFD&MI_00#test)",
        VK_PRIOR) == PhysicalAction::none;
    const bool otherKeyIgnored = logitechR400Action(r400, VK_F5)
        == PhysicalAction::none;
    constexpr UINT keyboardPacketSize = static_cast<UINT>(
        offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD));
    const bool keyboardPacketSizeAccepted =
        isRawKeyboardPacketSizeValid(keyboardPacketSize)
        && !isRawKeyboardPacketSizeValid(keyboardPacketSize - 1);
    const bool reservedHotkeysRejected =
        !isValidHotkey({0, VK_PRIOR})
        && !isValidHotkey({MOD_CONTROL, VK_NEXT})
        && isValidHotkey({0, VK_F13});
    std::cout << R"({"matchedDevice":)" << (matchedDevice ? "true" : "false")
              << R"(,"previous":")" << physicalActionName(previous)
              << R"(","next":")" << physicalActionName(next)
              << R"(","otherDeviceIgnored":)" << (otherDeviceIgnored ? "true" : "false")
              << R"(,"otherKeyIgnored":)" << (otherKeyIgnored ? "true" : "false")
              << R"(,"keyboardPacketSizeAccepted":)"
              << (keyboardPacketSizeAccepted ? "true" : "false")
              << R"(,"reservedHotkeysRejected":)"
              << (reservedHotkeysRejected ? "true" : "false")
              << "}\n";
    return matchedDevice && otherDeviceIgnored && otherKeyIgnored
            && keyboardPacketSizeAccepted && reservedHotkeysRejected
        ? 0
        : 1;
}

class NamedMutexLease {
public:
    explicit NamedMutexLease(const wchar_t *name) {
        handle_ = CreateMutexW(nullptr, FALSE, name);
        if (handle_ == nullptr) {
            return;
        }
        const DWORD result = WaitForSingleObject(handle_, 0);
        acquired_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }

    ~NamedMutexLease() {
        if (acquired_) {
            ReleaseMutex(handle_);
        }
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    NamedMutexLease(const NamedMutexLease &) = delete;
    NamedMutexLease &operator=(const NamedMutexLease &) = delete;

    [[nodiscard]] bool acquired() const {
        return acquired_;
    }

private:
    HANDLE handle_ = nullptr;
    bool acquired_ = false;
};

int runConfiguratorMutexSelfTest() {
    NamedMutexLease first(kConfiguratorMutexName);
    if (!first.acquired()) {
        return 2;
    }
    std::atomic<bool> secondAcquired{true};
    std::thread contender([&secondAcquired]() {
        NamedMutexLease second(kConfiguratorMutexName);
        secondAcquired.store(second.acquired());
    });
    contender.join();
    const bool exclusive = !secondAcquired.load();
    std::cout << R"({"exclusive":)" << (exclusive ? "true" : "false") << "}\n";
    return exclusive ? 0 : 1;
}

HotkeyPair defaultHotkeys() {
    return {
        {MOD_CONTROL | MOD_SHIFT, VK_F9},
        {MOD_CONTROL | MOD_SHIFT, VK_F10},
    };
}

bool isValidHotkey(const Hotkey &hotkey) {
    constexpr UINT supportedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
    return hotkey.virtualKey > 0
        && hotkey.virtualKey <= 0xFE
        && hotkey.virtualKey != VK_F12
        && !isLogitechR400ReservedHotkey(hotkey.virtualKey)
        && (hotkey.modifiers & ~supportedModifiers) == 0;
}

std::optional<DWORD> readRegistryDword(HKEY key, const wchar_t *name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        key,
        nullptr,
        name,
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    if (status != ERROR_SUCCESS || size != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<HotkeyPair> loadHotkeys() {
    HKEY key = nullptr;
    const LSTATUS opened = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        kSettingsRegistryPath,
        0,
        KEY_QUERY_VALUE,
        &key);
    if (opened == ERROR_FILE_NOT_FOUND) {
        return defaultHotkeys();
    }
    if (opened != ERROR_SUCCESS) {
        return std::nullopt;
    }

    const auto auctionModifiers = readRegistryDword(key, L"AuctionModifiers");
    const auto auctionVirtualKey = readRegistryDword(key, L"AuctionVirtualKey");
    const auto giveawayModifiers = readRegistryDword(key, L"GiveawayModifiers");
    const auto giveawayVirtualKey = readRegistryDword(key, L"GiveawayVirtualKey");
    RegCloseKey(key);
    if (!auctionModifiers || !auctionVirtualKey
        || !giveawayModifiers || !giveawayVirtualKey) {
        return std::nullopt;
    }

    HotkeyPair pair{
        {static_cast<UINT>(*auctionModifiers), static_cast<UINT>(*auctionVirtualKey)},
        {static_cast<UINT>(*giveawayModifiers), static_cast<UINT>(*giveawayVirtualKey)},
    };
    if (!isValidHotkey(pair.auction)
        || !isValidHotkey(pair.giveaway)
        || pair.auction == pair.giveaway) {
        return std::nullopt;
    }
    return pair;
}

bool clearStoredHotkeys() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kSettingsRegistryPath,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = true;
    for (const wchar_t *name : {
             L"AuctionModifiers",
             L"AuctionVirtualKey",
             L"GiveawayModifiers",
             L"GiveawayVirtualKey"}) {
        const LSTATUS status = RegDeleteValueW(key, name);
        ok = ok && (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}

bool writeStoredHotkey(const wchar_t *prefix, const Hotkey &hotkey) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kSettingsRegistryPath,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring modifiersName = std::wstring(prefix) + L"Modifiers";
    const std::wstring virtualKeyName = std::wstring(prefix) + L"VirtualKey";
    const DWORD modifiers = hotkey.modifiers;
    const DWORD virtualKey = hotkey.virtualKey;
    const bool ok = RegSetValueExW(
                        key,
                        modifiersName.c_str(),
                        0,
                        REG_DWORD,
                        reinterpret_cast<const BYTE *>(&modifiers),
                        sizeof(modifiers)) == ERROR_SUCCESS
        && RegSetValueExW(
               key,
               virtualKeyName.c_str(),
               0,
               REG_DWORD,
               reinterpret_cast<const BYTE *>(&virtualKey),
               sizeof(virtualKey)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

HotkeyTransactionBackend registryTransactionBackend() {
    return {
        clearStoredHotkeys,
        [](const Hotkey &hotkey) { return writeStoredHotkey(L"Auction", hotkey); },
        [](const Hotkey &hotkey) { return writeStoredHotkey(L"Giveaway", hotkey); },
        loadHotkeys,
    };
}

std::wstring hotkeyText(const Hotkey &hotkey) {
    std::wstring text;
    if ((hotkey.modifiers & MOD_CONTROL) != 0) {
        text += L"Ctrl+";
    }
    if ((hotkey.modifiers & MOD_ALT) != 0) {
        text += L"Alt+";
    }
    if ((hotkey.modifiers & MOD_SHIFT) != 0) {
        text += L"Shift+";
    }
    if ((hotkey.modifiers & MOD_WIN) != 0) {
        text += L"Win+";
    }
    if (hotkey.virtualKey >= VK_F1 && hotkey.virtualKey <= VK_F24) {
        text += L'F' + std::to_wstring(hotkey.virtualKey - VK_F1 + 1);
    } else if ((hotkey.virtualKey >= 'A' && hotkey.virtualKey <= 'Z')
               || (hotkey.virtualKey >= '0' && hotkey.virtualKey <= '9')) {
        text.push_back(static_cast<wchar_t>(hotkey.virtualKey));
    } else {
        text += L"VK" + std::to_wstring(hotkey.virtualKey);
    }
    return text;
}

std::string narrowAscii(const std::wstring &value) {
    return std::string(value.begin(), value.end());
}

int dumpEffectiveHotkeys() {
    const auto pair = loadHotkeys();
    if (!pair) {
        std::cout << R"({"error":"invalid registry hotkey configuration"})" << '\n';
        return 2;
    }
    std::cout << R"({"auction":")" << narrowAscii(hotkeyText(pair->auction))
              << R"(","giveaway":")" << narrowAscii(hotkeyText(pair->giveaway))
              << R"("})" << '\n';
    return 0;
}

std::wstring upper(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(character));
    });
    return value;
}

std::vector<std::wstring> split(const std::wstring &value, wchar_t delimiter) {
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        parts.push_back(value.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::optional<Hotkey> parseHotkey(const std::wstring &text) {
    Hotkey hotkey;
    bool hasKey = false;
    for (const std::wstring &rawPart : split(upper(text), L'+')) {
        if (rawPart == L"CTRL" || rawPart == L"CONTROL") {
            hotkey.modifiers |= MOD_CONTROL;
        } else if (rawPart == L"ALT") {
            hotkey.modifiers |= MOD_ALT;
        } else if (rawPart == L"SHIFT") {
            hotkey.modifiers |= MOD_SHIFT;
        } else if (rawPart == L"WIN" || rawPart == L"SUPER" || rawPart == L"META") {
            hotkey.modifiers |= MOD_WIN;
        } else if (rawPart.size() == 1 && rawPart[0] >= L'A' && rawPart[0] <= L'Z') {
            if (hasKey) {
                return std::nullopt;
            }
            hotkey.virtualKey = static_cast<UINT>(rawPart[0]);
            hasKey = true;
        } else if (rawPart.size() == 1 && rawPart[0] >= L'0' && rawPart[0] <= L'9') {
            if (hasKey) {
                return std::nullopt;
            }
            hotkey.virtualKey = static_cast<UINT>(rawPart[0]);
            hasKey = true;
        } else if (rawPart.size() >= 2 && rawPart[0] == L'F') {
            if (hasKey) {
                return std::nullopt;
            }
            try {
                const int number = std::stoi(rawPart.substr(1));
                if (number < 1 || number > 24) {
                    return std::nullopt;
                }
                hotkey.virtualKey = VK_F1 + static_cast<UINT>(number - 1);
                if (hotkey.virtualKey == VK_F12) {
                    return std::nullopt;
                }
                hasKey = true;
            } catch (...) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    return hasKey ? std::optional<Hotkey>{hotkey} : std::nullopt;
}

bool isRiskyUnmodified(const Hotkey &hotkey) {
    return hotkey.modifiers == 0
        && (hotkey.virtualKey < VK_F13 || hotkey.virtualKey > VK_F24);
}

int runValidation(int argc, wchar_t **argv) {
    if (argc != 4) {
        std::cerr << "usage: quick-swap-tools.exe --validate-hotkeys <auction> <giveaway>\n";
        return 64;
    }
    const auto auction = parseHotkey(argv[2]);
    const auto giveaway = parseHotkey(argv[3]);
    if (!auction || !giveaway) {
        std::cout << R"({"valid":false,"error":"invalid hotkey","warnings":[]})" << '\n';
        return 2;
    }
    if (auction->modifiers == giveaway->modifiers
        && auction->virtualKey == giveaway->virtualKey) {
        std::cout << R"({"valid":false,"error":"auction and giveaway hotkeys must differ","warnings":[]})" << '\n';
        return 2;
    }
    std::vector<std::string> warnings;
    if (isRiskyUnmodified(*auction)) {
        warnings.emplace_back("auction hotkey is an unmodified global key");
    }
    if (isRiskyUnmodified(*giveaway)) {
        warnings.emplace_back("giveaway hotkey is an unmodified global key");
    }
    std::cout << R"({"valid":true,"warnings":[)";
    for (std::size_t index = 0; index < warnings.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << '"' << warnings[index] << '"';
    }
    std::cout << "]}\n";
    return 0;
}

int runHotkeyTransactionSelfTest() {
    const HotkeyPair original{{0, VK_F20}, {0, VK_F21}};
    const HotkeyPair swapped{{0, VK_F21}, {0, VK_F20}};
    HotkeyPair stored = original;
    bool failAfterFinalMutation = false;

    HotkeyTransactionBackend backend;
    backend.clear = [&stored] {
        stored = {};
        return true;
    };
    backend.writeAuction = [&stored](const Hotkey &hotkey) {
        stored.auction = hotkey;
        return true;
    };
    backend.writeGiveaway = [&stored, &failAfterFinalMutation](const Hotkey &hotkey) {
        stored.giveaway = hotkey;
        if (failAfterFinalMutation) {
            failAfterFinalMutation = false;
            return false;
        }
        return true;
    };
    backend.read = [&stored] { return std::optional<HotkeyPair>{stored}; };

    const TransactionResult swap = applyHotkeyPair(original, swapped, backend);
    const bool swapSucceeded = swap.succeeded && stored == swapped;
    failAfterFinalMutation = true;
    const TransactionResult failed = applyHotkeyPair(swapped, original, backend);
    const bool rollbackRestored = !failed.succeeded
        && failed.rollbackRestored
        && stored == swapped;

    std::cout << R"({"swapSucceeded":)"
              << (swapSucceeded ? "true" : "false")
              << R"(,"rollbackRestored":)"
              << (rollbackRestored ? "true" : "false") << "}\n";
    return swapSucceeded && rollbackRestored ? 0 : 1;
}

bool readExact(std::istream &stream, char *destination, std::size_t size) {
    stream.read(destination, static_cast<std::streamsize>(size));
    return stream.gcount() == static_cast<std::streamsize>(size);
}

void writeFrameUnlocked(const std::string &payload) {
    const auto length = static_cast<std::uint32_t>(payload.size());
    std::cout.write(reinterpret_cast<const char *>(&length), sizeof(length));
    std::cout.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    std::cout.flush();
}

struct ParsedNativeMessage {
    bool valid = false;
    std::optional<std::string> type;
};

class DuplicateRejectingSax final : public nlohmann::json_sax<nlohmann::json> {
public:
    using json = nlohmann::json;
    using string_t = json::string_t;
    using number_integer_t = json::number_integer_t;
    using number_unsigned_t = json::number_unsigned_t;
    using number_float_t = json::number_float_t;
    using binary_t = json::binary_t;

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t &) override { return true; }
    bool string(string_t &) override { return true; }
    bool binary(binary_t &) override { return true; }

    bool start_object(std::size_t) override {
        objectKeys_.emplace_back();
        return true;
    }

    bool key(string_t &keyName) override {
        if (objectKeys_.empty()) {
            return false;
        }
        return objectKeys_.back().insert(keyName).second;
    }

    bool end_object() override {
        if (objectKeys_.empty()) {
            return false;
        }
        objectKeys_.pop_back();
        return true;
    }

    bool start_array(std::size_t) override { return true; }
    bool end_array() override { return true; }
    bool parse_error(
        std::size_t,
        const std::string &,
        const nlohmann::detail::exception &) override {
        return false;
    }

private:
    std::vector<std::unordered_set<std::string>> objectKeys_;
};

ParsedNativeMessage parseNativeMessage(const std::string &payload) {
    DuplicateRejectingSax duplicateRejector;
    if (!nlohmann::json::sax_parse(payload, &duplicateRejector)) {
        return {};
    }
    nlohmann::json message = nlohmann::json::parse(payload, nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        return {};
    }
    const auto type = message.find("type");
    if (type == message.end()) {
        return {true, std::nullopt};
    }
    if (!type->is_string()) {
        return {};
    }
    return {true, type->get<std::string>()};
}

enum class ReloadResult {
    noHost,
    succeeded,
    failed,
};

ReloadResult notifyRunningHost() {
    HANDLE reload = OpenEventW(EVENT_MODIFY_STATE, FALSE, kReloadEventName);
    if (reload == nullptr) {
        return ReloadResult::noHost;
    }
    HANDLE ok = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, kReloadOkEventName);
    HANDLE failed = OpenEventW(
        SYNCHRONIZE | EVENT_MODIFY_STATE,
        FALSE,
        kReloadFailedEventName);
    if (ok == nullptr || failed == nullptr) {
        if (ok != nullptr) {
            CloseHandle(ok);
        }
        if (failed != nullptr) {
            CloseHandle(failed);
        }
        CloseHandle(reload);
        return ReloadResult::failed;
    }

    ResetEvent(ok);
    ResetEvent(failed);
    const bool signaled = SetEvent(reload) != FALSE;
    const HANDLE acknowledgements[] = {ok, failed};
    const DWORD waitResult = signaled
        ? WaitForMultipleObjects(2, acknowledgements, FALSE, 3000)
        : WAIT_FAILED;
    CloseHandle(ok);
    CloseHandle(failed);
    CloseHandle(reload);
    return waitResult == WAIT_OBJECT_0
        ? ReloadResult::succeeded
        : ReloadResult::failed;
}

bool probeHotkeys(const HotkeyPair &pair) {
    const bool auction = RegisterHotKey(
        nullptr,
        100,
        pair.auction.modifiers | MOD_NOREPEAT,
        pair.auction.virtualKey);
    const bool giveaway = RegisterHotKey(
        nullptr,
        101,
        pair.giveaway.modifiers | MOD_NOREPEAT,
        pair.giveaway.virtualKey);
    if (auction) {
        UnregisterHotKey(nullptr, 100);
    }
    if (giveaway) {
        UnregisterHotKey(nullptr, 101);
    }
    return auction && giveaway;
}

class Configurator {
public:
    explicit Configurator(HINSTANCE instance)
        : instance_(instance) {}

    ~Configurator() {
        for (HGDIOBJ object : {
                 reinterpret_cast<HGDIOBJ>(eyebrowFont_),
                 reinterpret_cast<HGDIOBJ>(titleFont_),
                 reinterpret_cast<HGDIOBJ>(bodyBoldFont_),
                 reinterpret_cast<HGDIOBJ>(bodyFont_),
                 reinterpret_cast<HGDIOBJ>(windowBackgroundBrush_),
             }) {
            if (object != nullptr) {
                DeleteObject(object);
            }
        }
    }

    int run() {
        const auto loaded = loadHotkeys();
        if (!loaded) {
            MessageBoxW(
                nullptr,
                L"The saved hotkey configuration is incomplete or invalid. Reinstall or repair it before making changes.",
                L"Quick Swap Tools",
                MB_OK | MB_ICONERROR);
            return 2;
        }
        staged_ = *loaded;
        if (!createThemeResources()) {
            return 2;
        }

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = L"OniBytsQuickSwapConfigurator";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // The configurator owns this brush; do not also register it as a
        // class-owned background brush. WM_PAINT/WM_ERASEBKGND fill the window.
        windowClass.hbrBackground = nullptr;
        if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return 2;
        }

        window_ = CreateWindowExW(
            0,
            windowClass.lpszClassName,
            L"Quick Swap Tools — Configure controls",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            720,
            550,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) {
            return 2;
        }
        enableDarkTitleBar(window_);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            // During shortcut capture, navigation keys must reach WM_KEYDOWN
            // instead of being consumed by the dialog manager.
            if (recordingAction_ != 0 || !IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return 0;
    }

private:
    static constexpr int kAuctionButton = 101;
    static constexpr int kGiveawayButton = 102;
    static constexpr int kApplyButton = 201;
    static constexpr int kResetButton = 202;
    static constexpr int kCloseButton = 203;
    static constexpr int kEyebrowLabel = 301;
    static constexpr int kTitleLabel = 302;
    static constexpr int kSubtitleLabel = 303;
    static constexpr int kMutedLabel = 304;
    static constexpr int kStatusLabel = 305;
    static constexpr COLORREF kWindowBackground = RGB(11, 15, 20);
    static constexpr COLORREF kSurfaceBackground = RGB(22, 27, 34);
    static constexpr COLORREF kInputBackground = RGB(13, 17, 23);
    static constexpr COLORREF kBorderColor = RGB(48, 54, 61);
    static constexpr COLORREF kAccentColor = RGB(31, 111, 235);
    static constexpr COLORREF kAccentHoverColor = RGB(56, 139, 253);
    static constexpr COLORREF kFocusRingColor = RGB(191, 219, 254);
    static constexpr COLORREF kPrimaryText = RGB(230, 237, 243);
    static constexpr COLORREF kSecondaryText = RGB(139, 148, 158);

    static HFONT makeFont(int pixels, int weight) {
        return CreateFontW(
            -pixels,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    bool createThemeResources() {
        windowBackgroundBrush_ = CreateSolidBrush(kWindowBackground);
        bodyFont_ = makeFont(16, FW_NORMAL);
        bodyBoldFont_ = makeFont(16, FW_SEMIBOLD);
        titleFont_ = makeFont(30, FW_BOLD);
        eyebrowFont_ = makeFont(12, FW_BOLD);
        return windowBackgroundBrush_ != nullptr
            && bodyFont_ != nullptr
            && bodyBoldFont_ != nullptr
            && titleFont_ != nullptr
            && eyebrowFont_ != nullptr;
    }

    static void enableDarkTitleBar(HWND window) {
        constexpr DWORD kImmersiveDarkModeAttribute = 20; // DWMWA_USE_IMMERSIVE_DARK_MODE
        const BOOL enabled = TRUE;
        DwmSetWindowAttribute(
            window,
            kImmersiveDarkModeAttribute,
            &enabled,
            sizeof(enabled));
    }

    static LRESULT CALLBACK windowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {
        Configurator *self = reinterpret_cast<Configurator *>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            self = static_cast<Configurator *>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }
        return self != nullptr
            ? self->handleMessage(message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }

    HWND createControl(
        const wchar_t *className,
        const wchar_t *text,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        int identifier = 0,
        HFONT font = nullptr) {
        HWND control = CreateWindowExW(
            0,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            x,
            y,
            width,
            height,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
            instance_,
            nullptr);
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font != nullptr ? font : bodyFont_),
            TRUE);
        return control;
    }

    void createControls() {
        createControl(
            L"STATIC",
            L"QUICK SWAP TOOLS",
            SS_LEFT,
            28,
            22,
            640,
            20,
            kEyebrowLabel,
            eyebrowFont_);
        createControl(
            L"STATIC",
            L"Controls",
            SS_LEFT,
            28,
            48,
            640,
            42,
            kTitleLabel,
            titleFont_);
        createControl(
            L"STATIC",
            L"Choose the shortcuts that start your next auction and giveaway.",
            SS_LEFT,
            28,
            94,
            640,
            24,
            kSubtitleLabel);
        createControl(
            L"STATIC",
            L"LIVE ACTION SHORTCUTS",
            SS_LEFT,
            48,
            146,
            600,
            22,
            0,
            eyebrowFont_);
        createControl(L"STATIC", L"Next auction", SS_LEFT | SS_CENTERIMAGE, 48, 188, 150, 44);
        auctionButton_ = createControl(
            L"BUTTON",
            hotkeyText(staged_.auction).c_str(),
            BS_OWNERDRAW | WS_TABSTOP,
            210,
            188,
            430,
            44,
            kAuctionButton,
            bodyBoldFont_);
        createControl(L"STATIC", L"Next giveaway", SS_LEFT | SS_CENTERIMAGE, 48, 246, 150, 44);
        giveawayButton_ = createControl(
            L"BUTTON",
            hotkeyText(staged_.giveaway).c_str(),
            BS_OWNERDRAW | WS_TABSTOP,
            210,
            246,
            430,
            44,
            kGiveawayButton,
            bodyBoldFont_);
        createControl(
            L"STATIC",
            L"Logitech R400",
            SS_LEFT,
            48,
            338,
            600,
            22,
            0,
            eyebrowFont_);
        createControl(
            L"STATIC",
            L"Previous = Auction  •  Next = Giveaway. Page Up/Down stay available to the foreground app and are reserved here.",
            SS_LEFT,
            48,
            368,
            590,
            48,
            kMutedLabel);
        statusLabel_ = createControl(
            L"STATIC",
            L"Current mappings loaded from Windows.",
            SS_LEFT | SS_CENTERIMAGE,
            28,
            452,
            340,
            40,
            kStatusLabel);
        createControl(L"BUTTON", L"Apply changes", BS_OWNERDRAW | WS_TABSTOP, 382, 452, 132, 40, kApplyButton, bodyBoldFont_);
        createControl(L"BUTTON", L"Reset", BS_OWNERDRAW | WS_TABSTOP, 524, 452, 76, 40, kResetButton, bodyBoldFont_);
        createControl(L"BUTTON", L"Close", BS_OWNERDRAW | WS_TABSTOP, 610, 452, 70, 40, kCloseButton, bodyBoldFont_);
    }

    static void drawRoundedPanel(
        HDC dc,
        const RECT &rectangle,
        COLORREF fill,
        COLORREF border,
        int radius,
        int borderWidth = 1) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(
            dc,
            rectangle.left,
            rectangle.top,
            rectangle.right,
            rectangle.bottom,
            radius,
            radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void paintSurface(HDC dc) const {
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(dc, &client, windowBackgroundBrush_);
        drawRoundedPanel(dc, RECT{28, 132, 680, 310}, kSurfaceBackground, kBorderColor, 16);
        drawRoundedPanel(dc, RECT{28, 324, 680, 432}, kSurfaceBackground, kBorderColor, 16);
    }

    void drawButton(const DRAWITEMSTRUCT &item) const {
        const bool primary = item.CtlID == kApplyButton;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool focused = (item.itemState & ODS_FOCUS) != 0;
        COLORREF fill = primary ? kAccentColor : kInputBackground;
        COLORREF border = primary ? kAccentColor : kBorderColor;
        if (focused) {
            border = kFocusRingColor;
        }
        if (pressed) {
            fill = primary ? RGB(17, 88, 199) : kSurfaceBackground;
        }
        RECT buttonRect = item.rcItem;
        InflateRect(&buttonRect, -1, -1);
        drawRoundedPanel(item.hDC, buttonRect, fill, border, 12, focused ? 2 : 1);

        wchar_t text[128]{};
        GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, kPrimaryText);
        HFONT font = reinterpret_cast<HFONT>(
            SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = SelectObject(item.hDC, font != nullptr ? font : bodyBoldFont_);
        RECT textRect = item.rcItem;
        DrawTextW(
            item.hDC,
            text,
            -1,
            &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(item.hDC, oldFont);

    }

    static bool isModifierKey(UINT key) {
        return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT
            || key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL
            || key == VK_MENU || key == VK_LMENU || key == VK_RMENU
            || key == VK_LWIN || key == VK_RWIN;
    }

    static UINT currentModifiers() {
        UINT modifiers = 0;
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            modifiers |= MOD_CONTROL;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            modifiers |= MOD_ALT;
        }
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            modifiers |= MOD_SHIFT;
        }
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0
            || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
            modifiers |= MOD_WIN;
        }
        return modifiers;
    }

    void beginRecording(int action) {
        recordingAction_ = action;
        SetWindowTextW(
            action == kAuctionButton ? auctionButton_ : giveawayButton_,
            L"Press a key…");
        SetFocus(window_);
    }

    void recordKey(UINT key) {
        if (recordingAction_ == 0 || isModifierKey(key) || key == 0 || key > 0xFE) {
            return;
        }
        Hotkey &target = recordingAction_ == kAuctionButton
            ? staged_.auction
            : staged_.giveaway;
        target = {currentModifiers(), key};
        SetWindowTextW(
            recordingAction_ == kAuctionButton ? auctionButton_ : giveawayButton_,
            hotkeyText(target).c_str());
        recordingAction_ = 0;
    }

    bool reloadRunningHostOrRollback(const HotkeyPair &original) {
        const ReloadResult reload = notifyRunningHost();
        if (reload != ReloadResult::failed) {
            return true;
        }
        const TransactionResult rollback = applyHotkeyPair(
            staged_,
            original,
            registryTransactionBackend());
        const ReloadResult restoredReload = notifyRunningHost();
        const bool fullyRestored = rollback.succeeded
            && restoredReload != ReloadResult::failed;
        MessageBoxW(
            window_,
            fullyRestored
                ? L"Windows could not register both shortcuts. The previous bindings were restored. Choose different keys."
                : L"Windows could not register the shortcuts and rollback could not be verified. Close Firefox and run the configurator again.",
            L"Quick Swap Tools",
            MB_OK | MB_ICONERROR);
        return false;
    }

    void apply() {
        if (!isValidHotkey(staged_.auction) || !isValidHotkey(staged_.giveaway)) {
            MessageBoxW(window_, L"That key is reserved or cannot be used as a Windows global shortcut.", L"Quick Swap Tools", MB_OK | MB_ICONWARNING);
            return;
        }
        if (staged_.auction == staged_.giveaway) {
            MessageBoxW(window_, L"Auction and Giveaway must use different shortcuts.", L"Quick Swap Tools", MB_OK | MB_ICONWARNING);
            return;
        }
        if ((isRiskyUnmodified(staged_.auction) || isRiskyUnmodified(staged_.giveaway))
            && MessageBoxW(
                   window_,
                   L"An unmodified global key can trigger while you type in any application. Continue anyway?",
                   L"Quick Swap Tools",
                   MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return;
        }
        NamedMutexLease transactionLease(kConfiguratorMutexName);
        if (!transactionLease.acquired()) {
            MessageBoxW(
                window_,
                L"Another Quick Swap Tools configurator is applying controls. Close it or wait for it to finish, then try again.",
                L"Quick Swap Tools",
                MB_OK | MB_ICONWARNING);
            return;
        }
        const auto original = loadHotkeys();
        if (!original) {
            MessageBoxW(window_, L"Could not read the existing bindings. Nothing was changed.", L"Quick Swap Tools", MB_OK | MB_ICONERROR);
            return;
        }

        HANDLE runningHost = OpenEventW(EVENT_MODIFY_STATE, FALSE, kReloadEventName);
        const bool hostIsRunning = runningHost != nullptr;
        if (runningHost != nullptr) {
            CloseHandle(runningHost);
        }
        if (!hostIsRunning && !probeHotkeys(staged_)) {
            MessageBoxW(window_, L"One or both shortcuts are already reserved by Windows or another application. Nothing was changed.", L"Quick Swap Tools", MB_OK | MB_ICONWARNING);
            return;
        }

        const TransactionResult result = applyHotkeyPair(
            *original,
            staged_,
            registryTransactionBackend());
        if (!result.succeeded) {
            MessageBoxW(
                window_,
                result.rollbackRestored
                    ? L"Saving failed. The previous bindings were restored."
                    : L"Saving failed and rollback could not be verified. Do not rely on the shortcuts until the installation is repaired.",
                L"Quick Swap Tools",
                MB_OK | MB_ICONERROR);
            return;
        }
        if (!reloadRunningHostOrRollback(*original)) {
            staged_ = *original;
            refreshButtons();
            return;
        }
        setStatusText(L"Saved. Auction and Giveaway shortcuts are active.");
    }

    void refreshButtons() {
        SetWindowTextW(auctionButton_, hotkeyText(staged_.auction).c_str());
        SetWindowTextW(giveawayButton_, hotkeyText(staged_.giveaway).c_str());
        recordingAction_ = 0;
    }

    void setStatusText(const wchar_t *text) {
        SetWindowTextW(statusLabel_, text);
        RedrawWindow(
            statusLabel_,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            createControls();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window_, &paint);
            paintSurface(dc);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(window_, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, windowBackgroundBrush_);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const int identifier = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
            if (identifier == kStatusLabel) {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, kWindowBackground);
                SetTextColor(dc, kSecondaryText);
                return reinterpret_cast<LRESULT>(windowBackgroundBrush_);
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(
                dc,
                identifier == kEyebrowLabel
                    ? kAccentHoverColor
                    : (identifier == kSubtitleLabel
                          || identifier == kMutedLabel)
                        ? kSecondaryText
                        : kPrimaryText);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        case WM_DRAWITEM: {
            const auto *item = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
            if (item != nullptr && item->CtlType == ODT_BUTTON) {
                drawButton(*item);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case kAuctionButton:
            case kGiveawayButton:
                beginRecording(LOWORD(wParam));
                return 0;
            case kApplyButton:
                apply();
                return 0;
            case kResetButton:
                staged_ = defaultHotkeys();
                refreshButtons();
                setStatusText(L"Defaults selected. Apply changes to activate them.");
                return 0;
            case kCloseButton:
                DestroyWindow(window_);
                return 0;
            default:
                break;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (recordingAction_ != 0) {
                recordKey(static_cast<UINT>(wParam));
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND auctionButton_ = nullptr;
    HWND giveawayButton_ = nullptr;
    HWND statusLabel_ = nullptr;
    HBRUSH windowBackgroundBrush_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT bodyBoldFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT eyebrowFont_ = nullptr;
    HotkeyPair staged_{};
    int recordingAction_ = 0;
};

[[maybe_unused]] int runConfigurator(HINSTANCE instance) {
    Configurator configurator(instance);
    return configurator.run();
}

class NativeHost {
public:
    int run() {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        threadId_ = GetCurrentThreadId();

        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        const bool shortcutsDisabled = _wgetenv(L"QUICK_SWAP_NO_SHORTCUTS") != nullptr;
        std::thread writerThread;
        std::thread inputThread;
        std::thread controlThread;
        auto activateHotkeyOwnership = [this, &controlThread] {
            ownsHotkeyMutex_ = true;
            registerConfiguredHotkeys();
            openLogitechR400Input();
            openControlEvents();
            if (reloadEvent_ != nullptr && stopEvent_ != nullptr) {
                controlThread = std::thread([this] { watchControlEvents(); });
            }
        };
        if (!shortcutsDisabled) {
            hotkeyOwnerMutex_ = CreateMutexW(nullptr, FALSE, kHotkeyOwnerMutexName);
            if (hotkeyOwnerMutex_ != nullptr) {
                const DWORD ownership = WaitForSingleObject(hotkeyOwnerMutex_, 0);
                if (ownership == WAIT_OBJECT_0 || ownership == WAIT_ABANDONED) {
                    activateHotkeyOwnership();
                } else {
                    ownershipTimerId_ = SetTimer(nullptr, 0, 1000, nullptr);
                }
            }
        }
        writerThread = std::thread([this] { writeOutput(); });
        inputThread = std::thread([this] { readInput(); });
        int result = 0;
        while (true) {
            const BOOL received = GetMessageW(&message, nullptr, 0, 0);
            if (received == -1) {
                result = 2;
                break;
            }
            if (received == 0 || message.message == kInputClosedMessage) {
                result = inputResult_.load();
                break;
            }
            if (message.message == WM_HOTKEY) {
                if (message.wParam == kAuctionHotkeyId) {
                    trigger("auction", auctionLastRun_);
                } else if (message.wParam == kGiveawayHotkeyId) {
                    trigger("giveaway", giveawayLastRun_);
                }
            } else if (message.message == WM_TIMER
                && message.wParam == ownershipTimerId_
                && !ownsHotkeyMutex_
                && hotkeyOwnerMutex_ != nullptr) {
                const DWORD ownership = WaitForSingleObject(hotkeyOwnerMutex_, 0);
                if (ownership == WAIT_OBJECT_0 || ownership == WAIT_ABANDONED) {
                    KillTimer(nullptr, ownershipTimerId_);
                    ownershipTimerId_ = 0;
                    activateHotkeyOwnership();
                }
            } else if (message.message == kReloadHotkeysMessage && ownsHotkeyMutex_) {
                unregisterHotkeys();
                const bool registered = registerConfiguredHotkeys();
                if (registered && reloadOkEvent_ != nullptr) {
                    SetEvent(reloadOkEvent_);
                } else if (!registered && reloadFailedEvent_ != nullptr) {
                    SetEvent(reloadFailedEvent_);
                }
            } else {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        unregisterHotkeys();
        closeLogitechR400Input();
        if (stopEvent_ != nullptr) {
            SetEvent(stopEvent_);
        }
        if (inputThread.joinable()) {
            CancelSynchronousIo(reinterpret_cast<HANDLE>(inputThread.native_handle()));
            inputThread.join();
        }
        {
            const std::scoped_lock lock(outputMutex_);
            outputStopping_ = true;
        }
        outputCondition_.notify_one();
        if (writerThread.joinable()) {
            CancelSynchronousIo(reinterpret_cast<HANDLE>(writerThread.native_handle()));
            writerThread.join();
        }
        if (controlThread.joinable()) {
            controlThread.join();
        }
        closeControlEvents();
        if (ownershipTimerId_ != 0) {
            KillTimer(nullptr, ownershipTimerId_);
        }
        if (ownsHotkeyMutex_ && hotkeyOwnerMutex_ != nullptr) {
            ReleaseMutex(hotkeyOwnerMutex_);
        }
        if (hotkeyOwnerMutex_ != nullptr) {
            CloseHandle(hotkeyOwnerMutex_);
        }
        return result;
    }

private:
    static LRESULT CALLBACK rawInputWindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {
        NativeHost *self = reinterpret_cast<NativeHost *>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            self = static_cast<NativeHost *>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (message == WM_INPUT && self != nullptr) {
            self->handleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void openLogitechR400Input() {
        constexpr wchar_t className[] = L"OniBytsQuickSwapRawInput";
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = rawInputWindowProcedure;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = className;
        if (RegisterClassW(&windowClass) == 0
            && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        rawInputWindow_ = CreateWindowExW(
            0,
            className,
            className,
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            instance,
            this);
        if (rawInputWindow_ == nullptr) {
            return;
        }
        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x06;
        device.dwFlags = RIDEV_INPUTSINK;
        device.hwndTarget = rawInputWindow_;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            DestroyWindow(rawInputWindow_);
            rawInputWindow_ = nullptr;
            return;
        }
        r400InputRegistered_.store(true);
    }

    void closeLogitechR400Input() {
        if (rawInputWindow_ == nullptr) {
            return;
        }
        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x06;
        device.dwFlags = RIDEV_REMOVE;
        device.hwndTarget = nullptr;
        RegisterRawInputDevices(&device, 1, sizeof(device));
        DestroyWindow(rawInputWindow_);
        rawInputWindow_ = nullptr;
        r400InputRegistered_.store(false);
        r400PreviousPressed_ = false;
        r400NextPressed_ = false;
    }

    static std::wstring rawInputDeviceName(HANDLE device) {
        UINT characters = 0;
        if (GetRawInputDeviceInfoW(
                device,
                RIDI_DEVICENAME,
                nullptr,
                &characters) == static_cast<UINT>(-1)
            || characters == 0) {
            return {};
        }
        std::vector<wchar_t> name(characters + 1, L'\0');
        const UINT copied = GetRawInputDeviceInfoW(
            device,
            RIDI_DEVICENAME,
            name.data(),
            &characters);
        if (copied == static_cast<UINT>(-1) || copied == 0) {
            return {};
        }
        return std::wstring(name.data(), copied);
    }

    void handleRawInput(HRAWINPUT handle) {
        UINT size = 0;
        const UINT queryResult = GetRawInputData(
            handle,
            RID_INPUT,
            nullptr,
            &size,
            sizeof(RAWINPUTHEADER));
        if (queryResult != 0 || !isRawKeyboardPacketSizeValid(size)) {
            return;
        }
        std::vector<BYTE> buffer(size);
        const UINT copied = GetRawInputData(
            handle,
            RID_INPUT,
            buffer.data(),
            &size,
            sizeof(RAWINPUTHEADER));
        if (copied != size) {
            return;
        }
        const auto *input = reinterpret_cast<const RAWINPUT *>(buffer.data());
        if (input->header.dwType != RIM_TYPEKEYBOARD) {
            return;
        }
        const std::wstring deviceName = rawInputDeviceName(input->header.hDevice);
        const PhysicalAction action = logitechR400Action(
            deviceName,
            input->data.keyboard.VKey);
        bool *pressed = nullptr;
        if (action == PhysicalAction::auction) {
            pressed = &r400PreviousPressed_;
        } else if (action == PhysicalAction::giveaway) {
            pressed = &r400NextPressed_;
        } else {
            return;
        }
        if ((input->data.keyboard.Flags & RI_KEY_BREAK) != 0) {
            *pressed = false;
            return;
        }
        if (*pressed) {
            return;
        }
        *pressed = true;
        if (action == PhysicalAction::auction) {
            trigger("auction", auctionLastRun_);
        } else {
            trigger("giveaway", giveawayLastRun_);
        }
    }

    bool registerConfiguredHotkeys() {
        const auto hotkeys = loadHotkeys();
        if (!hotkeys) {
            return false;
        }
        auctionRegistered_ = RegisterHotKey(
            nullptr,
            kAuctionHotkeyId,
            hotkeys->auction.modifiers | MOD_NOREPEAT,
            hotkeys->auction.virtualKey);
        giveawayRegistered_ = RegisterHotKey(
            nullptr,
            kGiveawayHotkeyId,
            hotkeys->giveaway.modifiers | MOD_NOREPEAT,
            hotkeys->giveaway.virtualKey);
        if (!auctionRegistered_ || !giveawayRegistered_) {
            unregisterHotkeys();
            return false;
        }
        return true;
    }

    void openControlEvents() {
        reloadEvent_ = CreateEventW(nullptr, FALSE, FALSE, kReloadEventName);
        reloadOkEvent_ = CreateEventW(nullptr, FALSE, FALSE, kReloadOkEventName);
        reloadFailedEvent_ = CreateEventW(nullptr, FALSE, FALSE, kReloadFailedEventName);
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, kStopEventName);
        if (stopEvent_ != nullptr) {
            ResetEvent(stopEvent_);
        }
    }

    void closeControlEvents() {
        for (HANDLE handle : {reloadEvent_, reloadOkEvent_, reloadFailedEvent_, stopEvent_}) {
            if (handle != nullptr) {
                CloseHandle(handle);
            }
        }
    }

    void watchControlEvents() {
        const HANDLE events[] = {reloadEvent_, stopEvent_};
        while (true) {
            const DWORD result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0) {
                PostThreadMessageW(threadId_, kReloadHotkeysMessage, 0, 0);
            } else {
                return;
            }
        }
    }

    void unregisterHotkeys() {
        if (auctionRegistered_) {
            UnregisterHotKey(nullptr, kAuctionHotkeyId);
            auctionRegistered_ = false;
        }
        if (giveawayRegistered_) {
            UnregisterHotKey(nullptr, kGiveawayHotkeyId);
            giveawayRegistered_ = false;
        }
    }

    void readInput() {
        while (true) {
            std::uint32_t length = 0;
            if (!readExact(
                    std::cin,
                    reinterpret_cast<char *>(&length),
                    sizeof(length))) {
                break;
            }
            if (length == 0 || length > kMaxMessageBytes) {
                inputResult_.store(2);
                break;
            }
            std::string payload(length, '\0');
            if (!readExact(std::cin, payload.data(), payload.size())) {
                inputResult_.store(2);
                break;
            }
            const ParsedNativeMessage message = parseNativeMessage(payload);
            if (!message.valid) {
                inputResult_.store(2);
                break;
            }
            if (message.type == "hello") {
                std::ostringstream ready;
                ready << R"({"type":"ready","auctionShortcut":)"
                      << (auctionRegistered_ ? "true" : "false")
                      << R"(,"giveawayShortcut":)"
                      << (giveawayRegistered_ ? "true" : "false")
                      << R"(,"logitechR400":)"
                      << (r400InputRegistered_.load() ? "true" : "false") << '}';
                writeFrame(ready.str());
            }
        }
        PostThreadMessageW(threadId_, kInputClosedMessage, 0, 0);
    }

    void trigger(
        const std::string &action,
        std::chrono::steady_clock::time_point &lastRun) {
        const auto now = std::chrono::steady_clock::now();
        if (lastRun.time_since_epoch().count() != 0
            && now - lastRun < kBounceInterval) {
            return;
        }
        lastRun = now;
        const std::uint64_t sequence = ++commandSequence_;
        std::ostringstream command;
        command << R"({"type":"command","id":")"
                << std::hex << GetCurrentProcessId() << '-' << GetTickCount64()
                << '-' << sequence << R"(","action":")" << action << R"("})";
        writeFrame(command.str());
    }

    void writeFrame(const std::string &payload) {
        {
            const std::scoped_lock lock(outputMutex_);
            if (outputStopping_) {
                return;
            }
            if (outputQueue_.size() >= 64) {
                inputResult_.store(2);
                PostThreadMessageW(threadId_, kInputClosedMessage, 0, 0);
                return;
            }
            outputQueue_.push_back(payload);
        }
        outputCondition_.notify_one();
    }

    void writeOutput() {
        while (true) {
            std::string payload;
            {
                std::unique_lock lock(outputMutex_);
                outputCondition_.wait(lock, [this] {
                    return outputStopping_ || !outputQueue_.empty();
                });
                if (outputStopping_) {
                    return;
                }
                payload = std::move(outputQueue_.front());
                outputQueue_.pop_front();
            }
            writeFrameUnlocked(payload);
            if (!std::cout) {
                inputResult_.store(2);
                PostThreadMessageW(threadId_, kInputClosedMessage, 0, 0);
                return;
            }
        }
    }

    DWORD threadId_ = 0;
    std::atomic<bool> auctionRegistered_{false};
    std::atomic<bool> giveawayRegistered_{false};
    std::atomic<bool> r400InputRegistered_{false};
    std::atomic<int> inputResult_{0};
    std::atomic<std::uint64_t> commandSequence_{0};
    std::mutex outputMutex_;
    std::condition_variable outputCondition_;
    std::deque<std::string> outputQueue_;
    bool outputStopping_ = false;
    std::chrono::steady_clock::time_point auctionLastRun_{};
    std::chrono::steady_clock::time_point giveawayLastRun_{};
    HANDLE reloadEvent_ = nullptr;
    HANDLE reloadOkEvent_ = nullptr;
    HANDLE reloadFailedEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE hotkeyOwnerMutex_ = nullptr;
    HWND rawInputWindow_ = nullptr;
    bool r400PreviousPressed_ = false;
    bool r400NextPressed_ = false;
    bool ownsHotkeyMutex_ = false;
    UINT_PTR ownershipTimerId_ = 0;
};

int runHost() {
    NativeHost host;
    return host.run();
}
} // namespace

[[maybe_unused]] int runProgram(int argc, wchar_t **argv) {
    const std::wstring_view mode = argc >= 2 ? argv[1] : L"";
    if (mode == L"--validate-hotkeys") {
        return runValidation(argc, argv);
    }
    if (argc == 2 && mode == L"--self-test-hotkey-transaction") {
        return runHotkeyTransactionSelfTest();
    }
    if (argc == 2 && mode == L"--self-test-configurator-mutex") {
        return runConfiguratorMutexSelfTest();
    }
    if (argc == 2 && mode == L"--self-test-r400-profile") {
        return runLogitechR400ProfileSelfTest();
    }
    if (argc == 2 && mode == L"--dump-effective-hotkeys") {
        return dumpEffectiveHotkeys();
    }
    return runHost();
}

#ifdef QST_CONFIG_ONLY
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    return runConfigurator(instance);
}
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 64;
    }
    const int result = runProgram(argc, argv);
    LocalFree(argv);
    return result;
}
#endif
