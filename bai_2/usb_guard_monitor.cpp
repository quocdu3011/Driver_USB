#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

const fs::path kDefaultStatusFile{"/proc/usb_guard/status"};
const fs::path kSysUsbDevices{"/sys/bus/usb/devices"};
const fs::path kSysClassBlock{"/sys/class/block"};
const fs::path kProcMounts{"/proc/mounts"};

struct BlockPartition {
    std::string name;
    std::uint64_t size_bytes{};
    bool read_only{};
    std::vector<std::string> mountpoints;
};

struct BlockDevice {
    std::string name;
    std::uint64_t size_bytes{};
    bool read_only{};
    bool removable{};
    std::vector<std::string> mountpoints;
    std::vector<BlockPartition> partitions;
    fs::path ro_path;
};

struct ActiveDevice {
    int bus{};
    int dev{};
    std::string devpath;
    std::string vid;
    std::string pid;
    bool whitelisted{};
    unsigned int connect_count{};
    std::uint64_t connected_for_ms{};
    std::uint64_t total_connected_ms{};
    std::string manufacturer;
    std::string product;
    std::string serial;
    std::vector<BlockDevice> block_devices;
};

struct HistoryDevice {
    std::string vid;
    std::string pid;
    std::string serial;
    unsigned int connect_count{};
    unsigned int active_instances{};
    std::uint64_t total_connected_ms{};
};

struct StatusSnapshot {
    std::string module;
    bool whitelist_enabled{};
    std::string policy_mode;
    std::string whitelist_entries;
    std::string last_event{"none"};
    unsigned int active_count{};
    unsigned int history_count{};
    std::optional<ActiveDevice> last_device;
    std::vector<ActiveDevice> active_devices;
    std::vector<HistoryDevice> history_devices;
};

struct Options {
    fs::path status_file{kDefaultStatusFile};
    double watch_interval{};
    bool json{};
    bool tui{};
    bool readonly_untrusted{};
    bool dry_run{};
};

enum class TuiPage {
    Overview = 0,
    History = 1,
    Actions = 2,
};

enum class TuiStyle {
    Normal,
    Accent,
    Good,
    Warn,
    Muted,
    Selected,
};

struct TuiLine {
    std::string text;
    TuiStyle style{TuiStyle::Normal};
};

struct TerminalSize {
    int rows{24};
    int cols{80};
};

std::string normalized_policy_mode(const StatusSnapshot &status);
bool whitelist_policy_active(const StatusSnapshot &status);
std::string device_trust_label(const StatusSnapshot &status, bool whitelisted);
TuiStyle device_trust_style(const StatusSnapshot &status, bool whitelisted);

class TerminalGuard {
  public:
    TerminalGuard() = default;

    ~TerminalGuard() {
        restore();
    }

    void enable_raw_mode() {
        if (!isatty(STDIN_FILENO) || active_) {
            return;
        }

        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            throw std::runtime_error("Khong the doc thuoc tinh terminal.");
        }

        termios raw = original_;
        raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
        raw.c_iflag &= static_cast<unsigned long>(~(IXON | ICRNL));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            throw std::runtime_error("Khong the bat che do raw cho terminal.");
        }

        active_ = true;
        std::cout << "\033[?1049h\033[?25l";
        std::cout.flush();
    }

    void restore() {
        if (!active_) {
            return;
        }

        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        std::cout << "\033[?25h\033[?1049l";
        std::cout.flush();
        active_ = false;
    }

  private:
    termios original_{};
    bool active_{false};
};

const char *ansi_reset() {
    return "\033[0m";
}

const char *ansi_for_style(TuiStyle style) {
    switch (style) {
        case TuiStyle::Accent:
            return "\033[1;36m";
        case TuiStyle::Good:
            return "\033[1;32m";
        case TuiStyle::Warn:
            return "\033[1;31m";
        case TuiStyle::Muted:
            return "\033[0;90m";
        case TuiStyle::Selected:
            return "\033[30;46m";
        case TuiStyle::Normal:
        default:
            return "\033[0m";
    }
}

std::string page_name(TuiPage page) {
    switch (page) {
        case TuiPage::Overview:
            return "Overview";
        case TuiPage::History:
            return "History";
        case TuiPage::Actions:
            return "Actions";
    }

    return "Overview";
}

std::string trim(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

TerminalSize get_terminal_size() {
    TerminalSize size;
    winsize ws{};

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0) {
            size.rows = ws.ws_row;
        }
        if (ws.ws_col > 0) {
            size.cols = ws.ws_col;
        }
    }

    return size;
}

bool starts_with(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> wrap_text(const std::string &text, int width) {
    std::vector<std::string> lines;

    if (width <= 0) {
        lines.push_back(text);
        return lines;
    }

    std::istringstream words(text);
    std::string word;
    std::string current;

    while (words >> word) {
        if (static_cast<int>(word.size()) > width) {
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
            }

            for (std::size_t offset = 0; offset < word.size(); offset += width) {
                lines.push_back(word.substr(offset, width));
            }
            continue;
        }

        if (current.empty()) {
            current = word;
            continue;
        }

        if (static_cast<int>(current.size() + 1 + word.size()) <= width) {
            current += ' ';
            current += word;
        } else {
            lines.push_back(current);
            current = word;
        }
    }

    if (!current.empty()) {
        lines.push_back(current);
    }

    if (lines.empty()) {
        lines.push_back("");
    }

    return lines;
}

void append_wrapped_tui_line(std::vector<TuiLine> &lines, const std::string &text,
                             int width, TuiStyle style = TuiStyle::Normal) {
    const auto wrapped = wrap_text(text, width);
    for (const auto &line : wrapped) {
        lines.push_back({line, style});
    }
}

void append_blank_tui_line(std::vector<TuiLine> &lines) {
    lines.push_back({"", TuiStyle::Normal});
}

std::string read_text_file(const fs::path &path) {
    std::ifstream input(path);
    if (!input) {
        return "";
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string read_text_trimmed(const fs::path &path, const std::string &fallback = "") {
    const std::string text = trim(read_text_file(path));
    return text.empty() ? fallback : text;
}

std::uint64_t parse_u64(const std::string &value, std::uint64_t fallback = 0) {
    try {
        size_t idx = 0;
        const auto parsed = std::stoull(value, &idx, 10);
        if (idx != value.size()) {
            return fallback;
        }
        return parsed;
    } catch (...) {
        return fallback;
    }
}

unsigned int parse_uint(const std::string &value, unsigned int fallback = 0) {
    return static_cast<unsigned int>(parse_u64(value, fallback));
}

std::string strip_quotes(const std::string &value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::vector<std::string> tokenize_payload(const std::string &payload) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;

    for (char ch : payload) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            current.push_back(ch);
            continue;
        }

        if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

std::map<std::string, std::string> parse_token_pairs(const std::string &payload) {
    std::map<std::string, std::string> result;

    for (const auto &token : tokenize_payload(payload)) {
        const auto pos = token.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = token.substr(0, pos);
        const std::string value = strip_quotes(token.substr(pos + 1));
        result[key] = value;
    }

    return result;
}

ActiveDevice parse_active_device(const std::string &payload) {
    const auto raw = parse_token_pairs(payload);
    ActiveDevice device;

    device.bus = static_cast<int>(parse_uint(raw.count("bus") ? raw.at("bus") : "0"));
    device.dev = static_cast<int>(parse_uint(raw.count("dev") ? raw.at("dev") : "0"));
    device.devpath = raw.count("devpath") ? raw.at("devpath") : "";
    device.vid = raw.count("vid") ? raw.at("vid") : "";
    device.pid = raw.count("pid") ? raw.at("pid") : "";
    device.whitelisted = raw.count("whitelisted") && raw.at("whitelisted") == "1";
    device.connect_count = parse_uint(raw.count("connect_count") ? raw.at("connect_count") : "0");
    device.connected_for_ms = parse_u64(raw.count("connected_for_ms") ? raw.at("connected_for_ms") : "0");
    device.total_connected_ms = parse_u64(raw.count("total_connected_ms") ? raw.at("total_connected_ms") : "0");
    device.manufacturer = raw.count("manufacturer") ? raw.at("manufacturer") : "";
    device.product = raw.count("product") ? raw.at("product") : "";
    device.serial = raw.count("serial") ? raw.at("serial") : "";

    return device;
}

HistoryDevice parse_history_device(const std::string &payload) {
    const auto raw = parse_token_pairs(payload);
    HistoryDevice device;

    device.vid = raw.count("vid") ? raw.at("vid") : "";
    device.pid = raw.count("pid") ? raw.at("pid") : "";
    device.serial = raw.count("serial") ? raw.at("serial") : "";
    device.connect_count = parse_uint(raw.count("connect_count") ? raw.at("connect_count") : "0");
    device.active_instances = parse_uint(raw.count("active_instances") ? raw.at("active_instances") : "0");
    device.total_connected_ms = parse_u64(raw.count("total_connected_ms") ? raw.at("total_connected_ms") : "0");

    return device;
}

StatusSnapshot parse_status_text(const std::string &text) {
    StatusSnapshot status;
    std::istringstream input(text);
    std::string line;

    while (std::getline(input, line)) {
        if (trim(line).empty()) {
            continue;
        }

        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, pos));
        const std::string value = trim(line.substr(pos + 1));

        if (key == "module") {
            status.module = value;
        } else if (key == "whitelist_enabled") {
            status.whitelist_enabled = value == "1";
        } else if (key == "policy_mode") {
            status.policy_mode = value;
        } else if (key == "whitelist_entries") {
            status.whitelist_entries = value;
        } else if (key == "last_event") {
            status.last_event = value;
        } else if (key == "active_count") {
            status.active_count = parse_uint(value);
        } else if (key == "history_count") {
            status.history_count = parse_uint(value);
        } else if (key == "last_device") {
            if (value == "none") {
                status.last_device.reset();
            } else {
                status.last_device = parse_active_device(value);
            }
        } else if (starts_with(key, "active[")) {
            status.active_devices.push_back(parse_active_device(value));
        } else if (starts_with(key, "history[")) {
            status.history_devices.push_back(parse_history_device(value));
        }
    }

    return status;
}

std::unordered_map<std::string, std::vector<std::string>> parse_mounts() {
    std::unordered_map<std::string, std::vector<std::string>> mounts;
    std::ifstream input(kProcMounts);
    std::string line;

    while (std::getline(input, line)) {
        std::istringstream iss(line);
        std::string device;
        std::string mountpoint;

        if (!(iss >> device >> mountpoint)) {
            continue;
        }

        if (!starts_with(device, "/dev/")) {
            continue;
        }

        mounts[device].push_back(mountpoint);
    }

    return mounts;
}

std::vector<std::string> find_partition_names(const std::string &block_name) {
    std::vector<std::string> partitions;

    std::error_code ec;
    if (!fs::exists(kSysClassBlock, ec)) {
        return partitions;
    }

    for (const auto &entry : fs::directory_iterator(kSysClassBlock, ec)) {
        if (ec) {
            break;
        }

        const std::string name = entry.path().filename().string();
        if (name == block_name || !starts_with(name, block_name)) {
            continue;
        }

        if (!fs::exists(entry.path() / "partition", ec)) {
            continue;
        }

        partitions.push_back(name);
    }

    std::sort(partitions.begin(), partitions.end());
    return partitions;
}

BlockDevice collect_block_info(
    const std::string &block_name,
    const std::unordered_map<std::string, std::vector<std::string>> &mounts) {
    BlockDevice block;
    const fs::path block_path = kSysClassBlock / block_name;

    block.name = block_name;
    block.size_bytes = parse_u64(read_text_trimmed(block_path / "size")) * 512ULL;
    block.read_only = read_text_trimmed(block_path / "ro", "0") == "1";
    block.removable = read_text_trimmed(block_path / "removable", "0") == "1";
    block.ro_path = block_path / "ro";

    const auto mount_it = mounts.find("/dev/" + block_name);
    if (mount_it != mounts.end()) {
        block.mountpoints = mount_it->second;
    }

    for (const auto &partition_name : find_partition_names(block_name)) {
        BlockPartition part;
        const fs::path part_path = kSysClassBlock / partition_name;

        part.name = partition_name;
        part.size_bytes = parse_u64(read_text_trimmed(part_path / "size")) * 512ULL;
        part.read_only = read_text_trimmed(part_path / "ro", "0") == "1";

        const auto part_mount_it = mounts.find("/dev/" + partition_name);
        if (part_mount_it != mounts.end()) {
            part.mountpoints = part_mount_it->second;
        }

        block.partitions.push_back(std::move(part));
    }

    return block;
}

std::vector<std::string> find_usb_block_devices(const std::string &devpath) {
    std::set<std::string> block_names;
    const fs::path device_dir = kSysUsbDevices / devpath;
    std::error_code ec;

    if (!fs::exists(device_dir, ec)) {
        return {};
    }

    fs::recursive_directory_iterator it(device_dir, ec);
    fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (it->is_directory(ec) && it->path().filename() == "block") {
            for (const auto &block_entry : fs::directory_iterator(it->path(), ec)) {
                if (ec) {
                    break;
                }

                if (block_entry.is_directory(ec)) {
                    block_names.insert(block_entry.path().filename().string());
                }
            }
        }

        it.increment(ec);
    }

    return std::vector<std::string>(block_names.begin(), block_names.end());
}

void enrich_active_devices(StatusSnapshot &status) {
    const auto mounts = parse_mounts();

    for (auto &device : status.active_devices) {
        const auto block_names = find_usb_block_devices(device.devpath);
        for (const auto &block_name : block_names) {
            device.block_devices.push_back(collect_block_info(block_name, mounts));
        }
    }
}

std::vector<std::string> apply_readonly_policy(StatusSnapshot &status, bool dry_run) {
    std::vector<std::string> actions;

    if (status.policy_mode != "whitelist") {
        actions.push_back(
            "info policy skipped: whitelist is disabled, system is running in observe mode");
        return actions;
    }

    for (auto &device : status.active_devices) {
        if (device.whitelisted) {
            continue;
        }

        if (device.block_devices.empty()) {
            std::ostringstream warning;
            warning << "warn " << device.devpath
                    << ": untrusted device detected but no block device is resolved yet";
            actions.push_back(warning.str());
            continue;
        }

        bool planned_or_changed = false;
        for (auto &block : device.block_devices) {
            if (block.read_only) {
                continue;
            }

            if (dry_run) {
                std::ostringstream action;
                action << "[dry-run] set read-only " << device.devpath << " -> "
                       << block.name;
                actions.push_back(action.str());
                planned_or_changed = true;
                continue;
            }

            std::ofstream output(block.ro_path);
            if (!output) {
                std::ostringstream failure;
                failure << "failed " << block.name << ": cannot open " << block.ro_path;
                actions.push_back(failure.str());
                continue;
            }

            output << "1\n";
            if (!output) {
                std::ostringstream failure;
                failure << "failed " << block.name << ": cannot write " << block.ro_path;
                actions.push_back(failure.str());
                continue;
            }

            block.read_only = true;
            planned_or_changed = true;
            std::ostringstream action;
            action << "applied read-only " << device.devpath << " -> " << block.name;
            actions.push_back(action.str());
        }

        if (!planned_or_changed) {
            std::ostringstream info;
            info << "info " << device.devpath
                 << ": all resolved block devices are already read-only";
            actions.push_back(info.str());
        }
    }

    return actions;
}

std::string normalized_policy_mode(const StatusSnapshot &status) {
    if (!status.policy_mode.empty()) {
        return status.policy_mode;
    }
    return status.whitelist_enabled ? "whitelist" : "observe";
}

std::string format_duration_ms(std::uint64_t ms) {
    const std::uint64_t total_seconds = ms / 1000ULL;
    const std::uint64_t hours = total_seconds / 3600ULL;
    const std::uint64_t minutes = (total_seconds % 3600ULL) / 60ULL;
    const std::uint64_t seconds = total_seconds % 60ULL;

    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << hours << ':'
        << std::setw(2) << minutes << ':'
        << std::setw(2) << seconds;
    return out.str();
}

std::string format_size(std::uint64_t bytes) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    size_t unit_index = 0;

    while (value >= 1024.0 && unit_index < std::size(units) - 1) {
        value /= 1024.0;
        ++unit_index;
    }

    std::ostringstream out;
    if (unit_index == 0) {
        out << static_cast<std::uint64_t>(value) << ' ' << units[unit_index];
    } else {
        out << std::fixed << std::setprecision(1) << value << ' ' << units[unit_index];
    }
    return out.str();
}

std::string escape_json(const std::string &value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

void print_json_string(std::ostream &out, const std::string &value) {
    out << '"' << escape_json(value) << '"';
}

void render_json_block_partition(std::ostream &out, const BlockPartition &part, int indent) {
    const std::string pad(indent, ' ');
    const std::string next(indent + 2, ' ');

    out << pad << "{\n";
    out << next << "\"name\": ";
    print_json_string(out, part.name);
    out << ",\n";
    out << next << "\"size_bytes\": " << part.size_bytes << ",\n";
    out << next << "\"read_only\": " << (part.read_only ? "true" : "false") << ",\n";
    out << next << "\"mountpoints\": [";
    for (size_t i = 0; i < part.mountpoints.size(); ++i) {
        if (i) {
            out << ", ";
        }
        print_json_string(out, part.mountpoints[i]);
    }
    out << "]\n";
    out << pad << "}";
}

void render_json_block_device(std::ostream &out, const BlockDevice &block, int indent) {
    const std::string pad(indent, ' ');
    const std::string next(indent + 2, ' ');

    out << pad << "{\n";
    out << next << "\"name\": ";
    print_json_string(out, block.name);
    out << ",\n";
    out << next << "\"size_bytes\": " << block.size_bytes << ",\n";
    out << next << "\"read_only\": " << (block.read_only ? "true" : "false") << ",\n";
    out << next << "\"removable\": " << (block.removable ? "true" : "false") << ",\n";
    out << next << "\"mountpoints\": [";
    for (size_t i = 0; i < block.mountpoints.size(); ++i) {
        if (i) {
            out << ", ";
        }
        print_json_string(out, block.mountpoints[i]);
    }
    out << "],\n";
    out << next << "\"partitions\": [\n";
    for (size_t i = 0; i < block.partitions.size(); ++i) {
        render_json_block_partition(out, block.partitions[i], indent + 4);
        if (i + 1 != block.partitions.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << next << "]\n";
    out << pad << "}";
}

void render_json_active_device(std::ostream &out, const ActiveDevice &device, int indent) {
    const std::string pad(indent, ' ');
    const std::string next(indent + 2, ' ');

    out << pad << "{\n";
    out << next << "\"bus\": " << device.bus << ",\n";
    out << next << "\"dev\": " << device.dev << ",\n";
    out << next << "\"devpath\": ";
    print_json_string(out, device.devpath);
    out << ",\n";
    out << next << "\"vid\": ";
    print_json_string(out, device.vid);
    out << ",\n";
    out << next << "\"pid\": ";
    print_json_string(out, device.pid);
    out << ",\n";
    out << next << "\"whitelisted\": " << (device.whitelisted ? "true" : "false") << ",\n";
    out << next << "\"connect_count\": " << device.connect_count << ",\n";
    out << next << "\"connected_for_ms\": " << device.connected_for_ms << ",\n";
    out << next << "\"total_connected_ms\": " << device.total_connected_ms << ",\n";
    out << next << "\"manufacturer\": ";
    print_json_string(out, device.manufacturer);
    out << ",\n";
    out << next << "\"product\": ";
    print_json_string(out, device.product);
    out << ",\n";
    out << next << "\"serial\": ";
    print_json_string(out, device.serial);
    out << ",\n";
    out << next << "\"block_devices\": [\n";
    for (size_t i = 0; i < device.block_devices.size(); ++i) {
        render_json_block_device(out, device.block_devices[i], indent + 4);
        if (i + 1 != device.block_devices.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << next << "]\n";
    out << pad << "}";
}

void render_json_history_device(std::ostream &out, const HistoryDevice &device, int indent) {
    const std::string pad(indent, ' ');
    const std::string next(indent + 2, ' ');

    out << pad << "{\n";
    out << next << "\"vid\": ";
    print_json_string(out, device.vid);
    out << ",\n";
    out << next << "\"pid\": ";
    print_json_string(out, device.pid);
    out << ",\n";
    out << next << "\"serial\": ";
    print_json_string(out, device.serial);
    out << ",\n";
    out << next << "\"connect_count\": " << device.connect_count << ",\n";
    out << next << "\"active_instances\": " << device.active_instances << ",\n";
    out << next << "\"total_connected_ms\": " << device.total_connected_ms << "\n";
    out << pad << "}";
}

void render_json(const StatusSnapshot &status) {
    std::cout << "{\n";
    std::cout << "  \"module\": ";
    print_json_string(std::cout, status.module);
    std::cout << ",\n";
    std::cout << "  \"whitelist_enabled\": " << (status.whitelist_enabled ? "true" : "false") << ",\n";
    std::cout << "  \"policy_mode\": ";
    print_json_string(std::cout, normalized_policy_mode(status));
    std::cout << ",\n";
    std::cout << "  \"whitelist_entries\": ";
    print_json_string(std::cout, status.whitelist_entries);
    std::cout << ",\n";
    std::cout << "  \"last_event\": ";
    print_json_string(std::cout, status.last_event);
    std::cout << ",\n";
    std::cout << "  \"active_count\": " << status.active_count << ",\n";
    std::cout << "  \"history_count\": " << status.history_count << ",\n";
    std::cout << "  \"last_device\": ";
    if (status.last_device.has_value()) {
        render_json_active_device(std::cout, *status.last_device, 2);
    } else {
        std::cout << "null";
    }
    std::cout << ",\n";
    std::cout << "  \"active_devices\": [\n";
    for (size_t i = 0; i < status.active_devices.size(); ++i) {
        render_json_active_device(std::cout, status.active_devices[i], 4);
        if (i + 1 != status.active_devices.size()) {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"history_devices\": [\n";
    for (size_t i = 0; i < status.history_devices.size(); ++i) {
        render_json_history_device(std::cout, status.history_devices[i], 4);
        if (i + 1 != status.history_devices.size()) {
            std::cout << ",";
        }
        std::cout << "\n";
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
}

void render_text(const StatusSnapshot &status, const std::vector<std::string> &actions) {
    std::cout << "USB Guard Monitor\n";
    std::cout << "Module: " << (status.module.empty() ? "(unknown)" : status.module) << "\n";
    std::cout << "Whitelist: " << (status.whitelist_enabled ? "enabled" : "disabled") << "\n";
    std::cout << "Policy mode: " << normalized_policy_mode(status) << "\n";
    std::cout << "Whitelist entries: "
              << (status.whitelist_entries.empty() ? "(none)" : status.whitelist_entries)
              << "\n";
    std::cout << "Last event: " << status.last_event
              << " | Active: " << status.active_count
              << " | History: " << status.history_count << "\n";

    if (status.last_device.has_value()) {
        const auto &last = *status.last_device;
        std::cout << "Last device: " << last.devpath << ' '
                  << last.vid << ':' << last.pid
                  << " policy=" << device_trust_label(status, last.whitelisted) << "\n";
    }

    std::cout << "\nCurrent devices\n";
    std::cout << "---------------\n";

    if (status.active_devices.empty()) {
        std::cout << "(none)\n";
    } else {
        size_t index = 1;
        for (const auto &device : status.active_devices) {
            std::cout << index++ << ". " << device.devpath << " | "
                      << device.vid << ':' << device.pid
                      << " | policy=" << device_trust_label(status, device.whitelisted)
                      << "\n";
            std::cout << "   Product: " << device.manufacturer << " / "
                      << device.product << "\n";
            std::cout << "   Serial: " << device.serial << "\n";
            std::cout << "   Connects: " << device.connect_count
                      << " | Current: " << format_duration_ms(device.connected_for_ms)
                      << " | Total: " << format_duration_ms(device.total_connected_ms)
                      << "\n";

            if (device.block_devices.empty()) {
                std::cout << "   Block devices: (none)\n";
                continue;
            }

            for (const auto &block : device.block_devices) {
                std::cout << "   Block: " << block.name
                          << " | size=" << format_size(block.size_bytes)
                          << " | ro=" << (block.read_only ? "yes" : "no")
                          << " | mounts=";
                if (block.mountpoints.empty()) {
                    std::cout << "(none)\n";
                } else {
                    for (size_t i = 0; i < block.mountpoints.size(); ++i) {
                        if (i) {
                            std::cout << ", ";
                        }
                        std::cout << block.mountpoints[i];
                    }
                    std::cout << "\n";
                }

                for (const auto &part : block.partitions) {
                    std::cout << "     Partition: " << part.name
                              << " | size=" << format_size(part.size_bytes)
                              << " | ro=" << (part.read_only ? "yes" : "no")
                              << " | mounts=";
                    if (part.mountpoints.empty()) {
                        std::cout << "(none)\n";
                    } else {
                        for (size_t i = 0; i < part.mountpoints.size(); ++i) {
                            if (i) {
                                std::cout << ", ";
                            }
                            std::cout << part.mountpoints[i];
                        }
                        std::cout << "\n";
                    }
                }
            }
        }
    }

    std::cout << "\nHistory\n";
    std::cout << "-------\n";

    if (status.history_devices.empty()) {
        std::cout << "(none)\n";
    } else {
        size_t index = 1;
        for (const auto &device : status.history_devices) {
            std::cout << index++ << ". " << device.vid << ':' << device.pid
                      << " | serial=" << device.serial << "\n";
            std::cout << "   Connects: " << device.connect_count
                      << " | Active instances: " << device.active_instances
                      << " | Total: " << format_duration_ms(device.total_connected_ms)
                      << "\n";
        }
    }

    if (!actions.empty()) {
        std::cout << "\nPolicy actions\n";
        std::cout << "--------------\n";
        for (const auto &action : actions) {
            std::cout << "- " << action << "\n";
        }
    }
}

std::string truncate_line(const std::string &value, int width) {
    if (width <= 0) {
        return "";
    }

    if (static_cast<int>(value.size()) <= width) {
        return value;
    }

    if (width <= 3) {
        return value.substr(0, width);
    }

    return value.substr(0, width - 3) + "...";
}

std::string trusted_label(bool whitelisted) {
    return whitelisted ? "trusted" : "UNTRUSTED";
}

TuiStyle trusted_style(bool whitelisted) {
    return whitelisted ? TuiStyle::Good : TuiStyle::Warn;
}

bool whitelist_policy_active(const StatusSnapshot &status) {
    return normalized_policy_mode(status) == "whitelist";
}

std::string device_trust_label(const StatusSnapshot &status, bool whitelisted) {
    if (!whitelist_policy_active(status)) {
        return "monitor-only";
    }
    return trusted_label(whitelisted);
}

TuiStyle device_trust_style(const StatusSnapshot &status, bool whitelisted) {
    if (!whitelist_policy_active(status)) {
        return TuiStyle::Muted;
    }
    return trusted_style(whitelisted);
}

TuiStyle action_style(const std::string &action) {
    if (starts_with(action, "failed") || starts_with(action, "warn ")) {
        return TuiStyle::Warn;
    }
    if (starts_with(action, "[dry-run]")) {
        return TuiStyle::Accent;
    }
    if (starts_with(action, "applied")) {
        return TuiStyle::Good;
    }
    if (starts_with(action, "info ")) {
        return TuiStyle::Muted;
    }
    return TuiStyle::Normal;
}

unsigned int count_active_by_trust(const StatusSnapshot &status, bool whitelisted) {
    return static_cast<unsigned int>(std::count_if(
        status.active_devices.begin(), status.active_devices.end(),
        [whitelisted](const ActiveDevice &device) { return device.whitelisted == whitelisted; }));
}

void render_tui_cell(const std::string &value, int width, TuiStyle style) {
    const std::string clipped = truncate_line(value, width);
    std::cout << ansi_for_style(style) << clipped << ansi_reset();
    const int padding = std::max(0, width - static_cast<int>(clipped.size()));
    if (padding > 0) {
        std::cout << std::string(static_cast<std::size_t>(padding), ' ');
    }
}

void render_tui_border(int left_width, int right_width) {
    std::cout << '+'
              << std::string(std::max(1, left_width), '-')
              << '+'
              << std::string(std::max(1, right_width), '-')
              << "+\n";
}

void render_tui_tabs(TuiPage current_page, int width) {
    std::ostringstream out;
    out << "Tabs: ";

    const TuiPage pages[] = {TuiPage::Overview, TuiPage::History, TuiPage::Actions};
    for (const auto page : pages) {
        if (page == current_page) {
            out << '[' << page_name(page) << "] ";
        } else {
            out << page_name(page) << ' ';
        }
    }

    out << "| h/l or ←/→ switch page";
    render_tui_cell(out.str(), width, TuiStyle::Accent);
    std::cout << "\n";
}

std::vector<TuiLine> build_left_panel_lines(const StatusSnapshot &status,
                                            int selected_index,
                                            int width) {
    std::vector<TuiLine> lines;
    append_wrapped_tui_line(lines, "Connected USB mass storage", width, TuiStyle::Accent);
    append_blank_tui_line(lines);

    if (status.active_devices.empty()) {
        append_wrapped_tui_line(lines, "(none)", width, TuiStyle::Muted);
        return lines;
    }

    for (std::size_t i = 0; i < status.active_devices.size(); ++i) {
        const auto &device = status.active_devices[i];
        const bool selected = static_cast<int>(i) == selected_index;

        std::ostringstream summary;
        summary << (selected ? "> " : "  ")
                << device.devpath << "  " << device.vid << ':' << device.pid;
        lines.push_back({summary.str(), selected ? TuiStyle::Selected : TuiStyle::Normal});

        std::ostringstream detail;
        detail << "   " << device_trust_label(status, device.whitelisted)
               << " | " << device.product;
        lines.push_back({detail.str(),
                         selected ? TuiStyle::Selected
                                  : device_trust_style(status, device.whitelisted)});

        std::ostringstream stat;
        stat << "   count=" << device.connect_count
             << " current=" << format_duration_ms(device.connected_for_ms);
        lines.push_back({stat.str(), selected ? TuiStyle::Selected : TuiStyle::Muted});
        append_blank_tui_line(lines);
    }

    return lines;
}

std::vector<TuiLine> build_overview_lines(const StatusSnapshot &status, int selected_index,
                                          int width) {
    std::vector<TuiLine> lines;

    append_wrapped_tui_line(lines, "Overview", width, TuiStyle::Accent);
    append_wrapped_tui_line(
        lines,
        "Whitelist: " + std::string(status.whitelist_enabled ? "enabled" : "disabled") +
            " | Policy mode: " + normalized_policy_mode(status),
        width, status.whitelist_enabled ? TuiStyle::Good : TuiStyle::Muted);
    append_wrapped_tui_line(lines, "Entries: " + status.whitelist_entries, width,
                            TuiStyle::Muted);
    append_wrapped_tui_line(
        lines,
        "Last event: " + status.last_event + " | Active: " +
            std::to_string(status.active_count) + " | History: " +
            std::to_string(status.history_count),
        width);

    append_blank_tui_line(lines);
    append_wrapped_tui_line(lines, "Selected device", width, TuiStyle::Accent);

    if (status.active_devices.empty()) {
        append_wrapped_tui_line(lines, "(none)", width, TuiStyle::Muted);
        return lines;
    }

    const auto safe_index = std::clamp(selected_index, 0,
                                       static_cast<int>(status.active_devices.size() - 1));
    const auto &device = status.active_devices[static_cast<std::size_t>(safe_index)];

    append_wrapped_tui_line(lines, "Path: " + device.devpath, width);
    append_wrapped_tui_line(lines, "VID:PID: " + device.vid + ":" + device.pid, width);
    append_wrapped_tui_line(lines, "Trust: " + device_trust_label(status, device.whitelisted),
                            width, device_trust_style(status, device.whitelisted));
    append_wrapped_tui_line(lines, "Manufacturer: " + device.manufacturer, width);
    append_wrapped_tui_line(lines, "Product: " + device.product, width);
    append_wrapped_tui_line(lines, "Serial: " + device.serial, width);
    append_wrapped_tui_line(
        lines,
        "Connects: " + std::to_string(device.connect_count) +
            " | Current: " + format_duration_ms(device.connected_for_ms) +
            " | Total: " + format_duration_ms(device.total_connected_ms),
        width);

    append_blank_tui_line(lines);
    append_wrapped_tui_line(lines, "Block devices", width, TuiStyle::Accent);
    if (device.block_devices.empty()) {
        append_wrapped_tui_line(lines, "(none)", width, TuiStyle::Muted);
    } else {
        for (const auto &block : device.block_devices) {
            std::ostringstream summary;
            summary << block.name << " | " << format_size(block.size_bytes)
                    << " | ro=" << (block.read_only ? "yes" : "no");
            append_wrapped_tui_line(lines, summary.str(), width,
                                    block.read_only ? TuiStyle::Warn : TuiStyle::Normal);

            std::string mounts = block.mountpoints.empty() ? "(none)" : "";
            if (!block.mountpoints.empty()) {
                for (std::size_t i = 0; i < block.mountpoints.size(); ++i) {
                    if (i) {
                        mounts += ", ";
                    }
                    mounts += block.mountpoints[i];
                }
            }
            append_wrapped_tui_line(lines, "mounts: " + mounts, width, TuiStyle::Muted);

            for (const auto &part : block.partitions) {
                std::ostringstream partition_summary;
                partition_summary << part.name << " | " << format_size(part.size_bytes)
                                  << " | ro=" << (part.read_only ? "yes" : "no");
                append_wrapped_tui_line(lines, partition_summary.str(), width, TuiStyle::Muted);
            }
        }
    }

    return lines;
}

std::vector<TuiLine> build_history_lines(const StatusSnapshot &status, int width) {
    std::vector<TuiLine> lines;

    append_wrapped_tui_line(lines, "History", width, TuiStyle::Accent);
    append_wrapped_tui_line(lines,
                            "Thong ke theo VID:PID[:SERIAL] da ghi nhan tu luc nap module.",
                            width, TuiStyle::Muted);
    append_blank_tui_line(lines);

    if (status.history_devices.empty()) {
        append_wrapped_tui_line(lines, "(none)", width, TuiStyle::Muted);
        return lines;
    }

    for (const auto &entry : status.history_devices) {
        append_wrapped_tui_line(lines,
                                entry.vid + ":" + entry.pid + " serial=" + entry.serial,
                                width, TuiStyle::Normal);
        append_wrapped_tui_line(lines,
                                "connect_count=" + std::to_string(entry.connect_count) +
                                    " | active_instances=" +
                                    std::to_string(entry.active_instances),
                                width, TuiStyle::Good);
        append_wrapped_tui_line(lines,
                                "total_connected=" +
                                    format_duration_ms(entry.total_connected_ms),
                                width, TuiStyle::Muted);
        append_blank_tui_line(lines);
    }

    return lines;
}

std::vector<TuiLine> build_action_lines(const std::vector<std::string> &actions, int width) {
    std::vector<TuiLine> lines;

    append_wrapped_tui_line(lines, "Actions", width, TuiStyle::Accent);
    append_wrapped_tui_line(
        lines,
        "Trang nay hien thi cac hanh dong policy o user space, vi du dat read-only cho USB ngoai whitelist.",
        width, TuiStyle::Muted);
    append_blank_tui_line(lines);

    if (actions.empty()) {
        append_wrapped_tui_line(lines,
                                "Chua co action moi. Neu can, chay voi --readonly-untrusted.",
                                width, TuiStyle::Muted);
        append_wrapped_tui_line(lines,
                                "Tip: dung --dry-run de xem truoc cac thay doi.",
                                width, TuiStyle::Muted);
        return lines;
    }

    for (const auto &action : actions) {
        append_wrapped_tui_line(lines, action, width, action_style(action));
    }

    return lines;
}

std::vector<TuiLine> build_right_panel_lines(const StatusSnapshot &status,
                                             const std::vector<std::string> &actions,
                                             int selected_index, int width,
                                             TuiPage page) {
    switch (page) {
        case TuiPage::History:
            return build_history_lines(status, width);
        case TuiPage::Actions:
            return build_action_lines(actions, width);
        case TuiPage::Overview:
        default:
            return build_overview_lines(status, selected_index, width);
    }
}

void render_panel_row(const TuiLine *left, const TuiLine *right,
                      int left_width, int right_width) {
    std::cout << '|';
    if (left) {
        render_tui_cell(left->text, left_width, left->style);
    } else {
        render_tui_cell("", left_width, TuiStyle::Normal);
    }
    std::cout << '|';
    if (right) {
        render_tui_cell(right->text, right_width, right->style);
    } else {
        render_tui_cell("", right_width, TuiStyle::Normal);
    }
    std::cout << "|\n";
}

void render_popup_box(const TerminalSize &term, const std::vector<std::string> &popup_lines) {
    if (popup_lines.empty()) {
        return;
    }

    int content_width = 18;
    for (const auto &line : popup_lines) {
        content_width = std::max(content_width, static_cast<int>(line.size()));
    }
    content_width = std::min(content_width, std::max(20, term.cols - 10));

    std::vector<std::string> lines;
    lines.push_back("Policy actions");
    for (const auto &line : popup_lines) {
        const auto wrapped = wrap_text(line, content_width - 2);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    lines.push_back("Press x to close");

    const int box_width = std::min(term.cols - 4, content_width + 4);
    const int box_height = static_cast<int>(lines.size()) + 2;
    const int start_row = std::max(2, (term.rows - box_height) / 2);
    const int start_col = std::max(2, (term.cols - box_width) / 2);

    auto move_to = [](int row, int col) {
        std::cout << "\033[" << row << ';' << col << 'H';
    };

    move_to(start_row, start_col);
    std::cout << "\033[1;31m+" << std::string(box_width - 2, '=') << "+\033[0m";

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        move_to(start_row + 1 + i, start_col);
        std::cout << "\033[1;31m|\033[0m";
        render_tui_cell(lines[static_cast<std::size_t>(i)], box_width - 2,
                        i == 0 ? TuiStyle::Warn
                               : (i == static_cast<int>(lines.size()) - 1 ? TuiStyle::Muted
                                                                           : TuiStyle::Normal));
        std::cout << "\033[1;31m|\033[0m";
    }

    move_to(start_row + box_height - 1, start_col);
    std::cout << "\033[1;31m+" << std::string(box_width - 2, '=') << "+\033[0m";
}

void render_tui(const StatusSnapshot &status, const std::vector<std::string> &actions,
                int selected_index, TuiPage page, const Options &options,
                const std::vector<std::string> &popup_lines) {
    const TerminalSize term = get_terminal_size();
    const int total_panel_width = std::max(12, term.cols - 3);
    const int minimum_column_width =
        total_panel_width < 24 ? std::max(4, total_panel_width / 2) : 12;
    int left_width = total_panel_width < 52 ? total_panel_width / 2 : total_panel_width / 3;
    left_width = std::clamp(left_width, minimum_column_width,
                            std::max(minimum_column_width,
                                     total_panel_width - minimum_column_width));
    int right_width = total_panel_width - left_width;
    if (right_width < minimum_column_width) {
        right_width = minimum_column_width;
        left_width = std::max(minimum_column_width, total_panel_width - right_width);
    }
    const int inner_rows = std::max(6, term.rows - 9);
    const auto left_lines = build_left_panel_lines(status, selected_index, left_width);
    const auto right_lines =
        build_right_panel_lines(status, actions, selected_index, right_width, page);
    const unsigned int trusted_count = count_active_by_trust(status, true);
    const unsigned int untrusted_count = count_active_by_trust(status, false);

    std::cout << "\033[H\033[2J";
    std::ostringstream title;
    title << " USB Guard TUI | active=" << status.active_count
          << " | mode=" << normalized_policy_mode(status);
    if (whitelist_policy_active(status)) {
        title << " | trusted=" << trusted_count
              << " | untrusted=" << untrusted_count;
    }
    title << " | actions=" << actions.size() << " ";
    render_tui_cell(title.str(), term.cols, TuiStyle::Accent);
    std::cout << "\n";
    render_tui_tabs(page, term.cols);
    std::ostringstream meta;
    meta << " Status: " << options.status_file.string()
         << " | Policy: "
         << (options.readonly_untrusted
                 ? (options.dry_run ? "readonly-untrusted (dry-run)"
                                    : "readonly-untrusted (live)")
                 : "observe only");
    render_tui_cell(meta.str(), term.cols, TuiStyle::Muted);
    std::cout << "\n";
    render_tui_cell(" q quit | j/k move | h/l page | Tab next tab | r refresh | x close popup ",
                    term.cols, TuiStyle::Muted);
    std::cout << "\n";

    render_tui_border(left_width, right_width);
    for (int row = 0; row < inner_rows; ++row) {
        const TuiLine *left = row < static_cast<int>(left_lines.size())
                                  ? &left_lines[static_cast<std::size_t>(row)]
                                  : nullptr;
        const TuiLine *right = row < static_cast<int>(right_lines.size())
                                   ? &right_lines[static_cast<std::size_t>(row)]
                                   : nullptr;
        render_panel_row(left, right, left_width, right_width);
    }
    render_tui_border(left_width, right_width);

    std::ostringstream footer;
    if (status.last_device.has_value()) {
        const auto &last = *status.last_device;
        footer << "Last device: " << last.devpath << ' ' << last.vid << ':'
               << last.pid << " " << device_trust_label(status, last.whitelisted);
    } else {
        footer << "Last device: (none)";
    }
    render_tui_cell(footer.str(), term.cols,
                    status.last_device.has_value()
                        ? device_trust_style(status, status.last_device->whitelisted)
                        : TuiStyle::Muted);
    std::cout << "\n";

    if (!popup_lines.empty()) {
        render_popup_box(term, popup_lines);
    }
}

StatusSnapshot load_status(const fs::path &status_file, bool enrich) {
    if (!fs::exists(status_file)) {
        throw std::runtime_error("Khong tim thay " + status_file.string());
    }

    StatusSnapshot status = parse_status_text(read_text_file(status_file));
    status.policy_mode = normalized_policy_mode(status);
    if (enrich) {
        enrich_active_devices(status);
    }
    return status;
}

void print_usage(const char *progname) {
    std::cout
        << "Usage: " << progname << " [options]\n"
        << "  --status-file <path>       Duong dan toi file status\n"
        << "  --watch <seconds>          Theo doi lien tuc theo chu ky giay\n"
        << "  --tui                      Giao dien TUI terminal realtime, co tab Overview/History/Actions\n"
        << "  --json                     Xuat JSON\n"
        << "  --readonly-untrusted       Thu dat block device cua USB ngoai whitelist sang read-only khi policy whitelist dang bat\n"
        << "  --dry-run                  Chi mo phong, khong ghi vao sysfs\n"
        << "  --help                     Hien tro giup\n";
}

Options parse_args(int argc, char **argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--status-file") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Thieu gia tri cho --status-file");
            }
            options.status_file = argv[++i];
        } else if (arg == "--watch") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Thieu gia tri cho --watch");
            }
            options.watch_interval = std::stod(argv[++i]);
        } else if (arg == "--tui") {
            options.tui = true;
        } else if (arg == "--json") {
            options.json = true;
        } else if (arg == "--readonly-untrusted") {
            options.readonly_untrusted = true;
        } else if (arg == "--dry-run") {
            options.dry_run = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("Tuy chon khong hop le: " + arg);
        }
    }

    return options;
}

int run_once(const Options &options) {
    StatusSnapshot status = load_status(options.status_file, true);
    std::vector<std::string> actions;

    if (options.readonly_untrusted && !options.dry_run &&
        whitelist_policy_active(status) && geteuid() != 0) {
        throw std::runtime_error(
            "Can chay bang quyen root neu muon ap chinh sach read-only.");
    }

    if (options.readonly_untrusted) {
        actions = apply_readonly_policy(status, options.dry_run);
    }

    if (options.json) {
        render_json(status);
    } else {
        render_text(status, actions);
    }

    return 0;
}

int clamp_selected_index(const StatusSnapshot &status, int selected_index) {
    if (status.active_devices.empty()) {
        return 0;
    }

    return std::clamp(selected_index, 0,
                      static_cast<int>(status.active_devices.size()) - 1);
}

int read_tui_key(double timeout_seconds) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    timeval timeout{};
    timeout.tv_sec = static_cast<int>(timeout_seconds);
    timeout.tv_usec = static_cast<int>((timeout_seconds - timeout.tv_sec) * 1000000.0);

    const int ready = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        return -1;
    }

    unsigned char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) != 1) {
        return -1;
    }

    if (ch == '\033') {
        unsigned char seq[2]{};
        if (read(STDIN_FILENO, &seq[0], 1) == 1 &&
            read(STDIN_FILENO, &seq[1], 1) == 1 &&
            seq[0] == '[') {
            if (seq[1] == 'A') {
                return 'k';
            }
            if (seq[1] == 'B') {
                return 'j';
            }
            if (seq[1] == 'C') {
                return 'l';
            }
            if (seq[1] == 'D') {
                return 'h';
            }
        }
        return -1;
    }

    return ch;
}

std::string build_actions_signature(const std::vector<std::string> &actions) {
    std::ostringstream out;

    for (const auto &action : actions) {
        out << action << '\n';
    }

    return out.str();
}

TuiPage next_page(TuiPage page) {
    switch (page) {
        case TuiPage::Overview:
            return TuiPage::History;
        case TuiPage::History:
            return TuiPage::Actions;
        case TuiPage::Actions:
        default:
            return TuiPage::Overview;
    }
}

TuiPage previous_page(TuiPage page) {
    switch (page) {
        case TuiPage::Overview:
            return TuiPage::Actions;
        case TuiPage::History:
            return TuiPage::Overview;
        case TuiPage::Actions:
        default:
            return TuiPage::History;
    }
}

int run_tui(const Options &options) {
    TerminalGuard guard;
    guard.enable_raw_mode();

    const double refresh_interval = options.watch_interval > 0.0 ? options.watch_interval : 1.0;
    int selected_index = 0;
    TuiPage current_page = TuiPage::Overview;
    std::vector<std::string> popup_lines;
    std::string last_actions_signature;
    int popup_ticks = 0;

    while (true) {
        try {
            StatusSnapshot status = load_status(options.status_file, true);
            std::vector<std::string> actions;

            if (options.readonly_untrusted && !options.dry_run &&
                whitelist_policy_active(status) && geteuid() != 0) {
                throw std::runtime_error(
                    "Can chay bang quyen root neu muon ap chinh sach read-only.");
            }

            if (options.readonly_untrusted) {
                actions = apply_readonly_policy(status, options.dry_run);
            }

            const std::string signature = build_actions_signature(actions);
            if (!actions.empty() && signature != last_actions_signature) {
                popup_lines = actions;
                popup_ticks = 6;
                last_actions_signature = signature;
            } else if (actions.empty()) {
                last_actions_signature.clear();
            }

            selected_index = clamp_selected_index(status, selected_index);
            render_tui(status, actions, selected_index, current_page, options, popup_lines);
            std::cout.flush();
        } catch (const std::exception &ex) {
            std::cout << "\033[H\033[2J";
            std::cout << "USB Guard TUI\n";
            std::cout << "Error: " << ex.what() << "\n";
            std::cout << "Nhan q de thoat, r de thu lai.\n";
            std::cout.flush();
        }

        const int key = read_tui_key(refresh_interval);
        if (key == 'q' || key == 'Q') {
            break;
        }
        if (key == 'j' || key == 'J') {
            ++selected_index;
        } else if (key == 'k' || key == 'K') {
            --selected_index;
        } else if (key == 'l' || key == 'L' || key == '\t') {
            current_page = next_page(current_page);
        } else if (key == 'h' || key == 'H') {
            current_page = previous_page(current_page);
        } else if (key == 'x' || key == 'X') {
            popup_lines.clear();
            popup_ticks = 0;
        } else if (key == 'r' || key == 'R' || key == -1) {
            if (key == -1 && popup_ticks > 0) {
                --popup_ticks;
                if (popup_ticks == 0) {
                    popup_lines.clear();
                }
            }
            continue;
        }
    }

    return 0;
}

int run_watch(const Options &options) {
    while (true) {
        try {
            StatusSnapshot status = load_status(options.status_file, true);
            std::vector<std::string> actions;
            if (options.readonly_untrusted && !options.dry_run &&
                whitelist_policy_active(status) && geteuid() != 0) {
                throw std::runtime_error(
                    "Can chay bang quyen root neu muon ap chinh sach read-only.");
            }
            if (options.readonly_untrusted) {
                actions = apply_readonly_policy(status, options.dry_run);
            }

            std::cout << "\033[2J\033[H";
            const auto now = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now());
            std::cout << std::put_time(std::localtime(&now), "%F %T") << "\n";

            if (options.json) {
                render_json(status);
            } else {
                render_text(status, actions);
            }

            std::cout.flush();
        } catch (const std::exception &ex) {
            std::cout << "\033[2J\033[H";
            std::cout << ex.what() << "\n";
            std::cout << "Module usb_guard co the chua duoc nap.\n";
            std::cout.flush();
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(options.watch_interval));
    }
}

}  // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parse_args(argc, argv);

        if (options.tui) {
            return run_tui(options);
        }

        if (options.watch_interval > 0.0) {
            return run_watch(options);
        }

        return run_once(options);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
