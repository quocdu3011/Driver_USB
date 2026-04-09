#include <gtk/gtk.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
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
const fs::path kDefaultConfigFile{"/etc/usb_guard.conf"};
const fs::path kSysUsbDevices{"/sys/bus/usb/devices"};
const fs::path kSysClassBlock{"/sys/class/block"};
const fs::path kProcMounts{"/proc/mounts"};
const char kUnknownValue[] = "N/A";
constexpr std::size_t kMaxLogLines = 400;

struct BlockPartition {
    std::string name;
    std::uint64_t size_bytes{};
    bool read_only{};
    std::vector<std::string> mountpoints;
};

struct BlockDevice {
    std::string name;
    std::uint64_t size_bytes{};
    bool removable{};
    bool read_only{};
    std::vector<std::string> mountpoints;
    std::vector<BlockPartition> partitions;
    std::uint64_t read_bytes_total{};
    std::uint64_t write_bytes_total{};
    double read_bps{};
    double write_bps{};
};

struct ActiveDevice {
    int bus{};
    int dev{};
    std::string devpath;
    std::string vid;
    std::string pid;
    bool authorized{true};
    bool authorization_supported{};
    bool whitelisted{};
    unsigned int connect_count{};
    std::uint64_t connected_for_ms{};
    std::uint64_t total_connected_ms{};
    std::string manufacturer;
    std::string product;
    std::string serial;
    fs::path authorized_path;
    std::vector<BlockDevice> block_devices;
    std::uint64_t read_bytes_total{};
    std::uint64_t write_bytes_total{};
    double read_bps{};
    double write_bps{};
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

struct GuiOptions {
    fs::path status_file{kDefaultStatusFile};
    fs::path config_file{kDefaultConfigFile};
};

enum class SavedAccessMode {
    None = 0,
    ReadOnly = 1,
    ReadWrite = 2,
};

struct PersistentConfig {
    std::vector<std::string> whitelist_rules;
    std::map<std::string, SavedAccessMode> access_policies;
};

struct BlockIoCounters {
    std::uint64_t read_bytes{};
    std::uint64_t write_bytes{};
};

struct BlockIoSample {
    std::uint64_t read_bytes{};
    std::uint64_t write_bytes{};
    std::chrono::steady_clock::time_point timestamp{};
};

enum ActiveColumns {
    ACTIVE_COL_INDEX = 0,
    ACTIVE_COL_DEVICE,
    ACTIVE_COL_TRUST,
    ACTIVE_COL_ACCESS,
    ACTIVE_COL_PRODUCT,
    ACTIVE_COL_IO_RATE,
    ACTIVE_COL_IO_TOTAL,
    ACTIVE_COL_MOUNTS,
    ACTIVE_COL_TRUST_COLOR,
    ACTIVE_COL_COUNT
};

enum HistoryColumns {
    HISTORY_COL_VIDPID = 0,
    HISTORY_COL_SERIAL,
    HISTORY_COL_CONNECT_COUNT,
    HISTORY_COL_ACTIVE_INSTANCES,
    HISTORY_COL_TOTAL_DURATION,
    HISTORY_COL_COUNT
};

struct GuiState {
    GuiOptions options;
    GtkApplication *application{};
    GtkWidget *window{};
    GtkWidget *banner_label{};
    GtkWidget *module_value{};
    GtkWidget *mode_value{};
    GtkWidget *whitelist_value{};
    GtkWidget *event_value{};
    GtkWidget *active_value{};
    GtkWidget *history_value{};
    GtkWidget *trusted_value{};
    GtkWidget *untrusted_value{};
    GtkWidget *status_label{};
    GtkWidget *auto_refresh_check{};
    GtkWidget *interval_spin{};
    GtkWidget *readonly_button{};
    GtkWidget *readwrite_button{};
    GtkWidget *active_tree{};
    GtkWidget *history_tree{};
    GtkListStore *active_store{};
    GtkListStore *history_store{};
    GtkTextBuffer *detail_buffer{};
    GtkTextBuffer *log_buffer{};
    guint refresh_source_id{};
    StatusSnapshot current_status;
    PersistentConfig persistent_config;
    std::unordered_map<std::string, BlockIoSample> io_samples;
    std::map<std::string, std::string> previous_active_labels;
    std::deque<std::string> log_lines;
    bool first_refresh_done{false};
    bool persistent_config_loaded{};
    bool persistent_config_present{};
    bool whitelist_sync_attempted{};
    bool whitelist_sync_warning_emitted{};
    bool access_policy_warning_emitted{};
    std::string last_error;
};

void append_log(GuiState *state, const std::string &level,
                const std::string &message);
void update_access_action_buttons(GuiState *state);

std::string trim(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool starts_with(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
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

std::string read_text_trimmed(const fs::path &path,
                              const std::string &fallback = "") {
    const std::string text = trim(read_text_file(path));
    return text.empty() ? fallback : text;
}

std::uint64_t parse_u64(const std::string &value,
                        std::uint64_t fallback = 0) {
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

std::string unescape_mount_field(const std::string &value) {
    std::string result;

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 3 < value.size() &&
            std::isdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(value[i + 2])) &&
            std::isdigit(static_cast<unsigned char>(value[i + 3]))) {
            const int decoded = (value[i + 1] - '0') * 64 +
                                (value[i + 2] - '0') * 8 +
                                (value[i + 3] - '0');
            result.push_back(static_cast<char>(decoded));
            i += 3;
            continue;
        }

        result.push_back(value[i]);
    }

    return result;
}

std::string escape_config_field(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

std::string unescape_config_field(const std::string &value) {
    std::string unescaped;
    unescaped.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[i + 1];
            if (next == '\\') {
                unescaped.push_back('\\');
                ++i;
                continue;
            }
            if (next == 't') {
                unescaped.push_back('\t');
                ++i;
                continue;
            }
            if (next == 'n') {
                unescaped.push_back('\n');
                ++i;
                continue;
            }
        }

        unescaped.push_back(value[i]);
    }

    return unescaped;
}

std::vector<std::string> split_tab_fields(const std::string &line) {
    std::vector<std::string> fields;
    std::string current;

    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\t') {
            fields.push_back(unescape_config_field(current));
            current.clear();
            continue;
        }

        current.push_back(line[i]);
    }

    fields.push_back(unescape_config_field(current));
    return fields;
}

std::string saved_access_mode_to_string(SavedAccessMode mode) {
    switch (mode) {
        case SavedAccessMode::ReadOnly:
            return "readonly";
        case SavedAccessMode::ReadWrite:
            return "readwrite";
        case SavedAccessMode::None:
        default:
            return "none";
    }
}

std::string saved_access_mode_label(SavedAccessMode mode) {
    switch (mode) {
        case SavedAccessMode::ReadOnly:
            return "Chỉ đọc";
        case SavedAccessMode::ReadWrite:
            return "Đọc/ghi";
        case SavedAccessMode::None:
        default:
            return "Không lưu";
    }
}

SavedAccessMode parse_saved_access_mode(const std::string &value) {
    const std::string normalized = trim(value);
    if (normalized == "readonly") {
        return SavedAccessMode::ReadOnly;
    }
    if (normalized == "readwrite") {
        return SavedAccessMode::ReadWrite;
    }
    return SavedAccessMode::None;
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

std::map<std::string, std::string> parse_token_pairs(
    const std::string &payload) {
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
    device.connect_count =
        parse_uint(raw.count("connect_count") ? raw.at("connect_count") : "0");
    device.connected_for_ms =
        parse_u64(raw.count("connected_for_ms") ? raw.at("connected_for_ms") : "0");
    device.total_connected_ms =
        parse_u64(raw.count("total_connected_ms") ? raw.at("total_connected_ms") : "0");
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
    device.connect_count =
        parse_uint(raw.count("connect_count") ? raw.at("connect_count") : "0");
    device.active_instances =
        parse_uint(raw.count("active_instances") ? raw.at("active_instances") : "0");
    device.total_connected_ms =
        parse_u64(raw.count("total_connected_ms") ? raw.at("total_connected_ms") : "0");

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

        mounts[unescape_mount_field(device)].push_back(unescape_mount_field(mountpoint));
    }

    return mounts;
}

bool read_block_readonly_state(const std::string &block_name) {
    return read_text_trimmed(kSysClassBlock / block_name / "ro", "0") == "1";
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
    block.removable = read_text_trimmed(block_path / "removable", "0") == "1";
    block.read_only = read_block_readonly_state(block_name);

    const auto mount_it = mounts.find("/dev/" + block_name);
    if (mount_it != mounts.end()) {
        block.mountpoints = mount_it->second;
    }

    for (const auto &partition_name : find_partition_names(block_name)) {
        BlockPartition part;
        const fs::path part_path = kSysClassBlock / partition_name;

        part.name = partition_name;
        part.size_bytes = parse_u64(read_text_trimmed(part_path / "size")) * 512ULL;
        part.read_only = read_block_readonly_state(partition_name);

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

std::optional<BlockIoCounters> read_block_io_counters(const std::string &block_name) {
    std::istringstream input(read_text_file(kSysClassBlock / block_name / "stat"));
    std::vector<std::uint64_t> fields;
    std::uint64_t value = 0;

    while (input >> value) {
        fields.push_back(value);
    }

    if (fields.size() < 7) {
        return std::nullopt;
    }

    BlockIoCounters counters;
    counters.read_bytes = fields[2] * 512ULL;
    counters.write_bytes = fields[6] * 512ULL;
    return counters;
}

void enrich_authorization(ActiveDevice &device) {
    const fs::path authorized_path = kSysUsbDevices / device.devpath / "authorized";

    device.authorized_path = authorized_path;
    device.authorization_supported = fs::exists(authorized_path);
    if (!device.authorization_supported) {
        device.authorized = true;
        return;
    }

    device.authorized = read_text_trimmed(authorized_path, "1") != "0";
}

void update_io_metrics(StatusSnapshot &status,
                       std::unordered_map<std::string, BlockIoSample> &samples) {
    const auto now = std::chrono::steady_clock::now();

    for (auto &device : status.active_devices) {
        device.read_bytes_total = 0;
        device.write_bytes_total = 0;
        device.read_bps = 0.0;
        device.write_bps = 0.0;

        for (auto &block : device.block_devices) {
            block.read_bytes_total = 0;
            block.write_bytes_total = 0;
            block.read_bps = 0.0;
            block.write_bps = 0.0;

            const auto counters = read_block_io_counters(block.name);
            if (!counters.has_value()) {
                continue;
            }

            block.read_bytes_total = counters->read_bytes;
            block.write_bytes_total = counters->write_bytes;

            const auto previous_it = samples.find(block.name);
            if (previous_it != samples.end()) {
                const auto elapsed =
                    std::chrono::duration<double>(now - previous_it->second.timestamp)
                        .count();
                if (elapsed > 0.0 &&
                    counters->read_bytes >= previous_it->second.read_bytes &&
                    counters->write_bytes >= previous_it->second.write_bytes) {
                    block.read_bps =
                        static_cast<double>(counters->read_bytes -
                                            previous_it->second.read_bytes) /
                        elapsed;
                    block.write_bps =
                        static_cast<double>(counters->write_bytes -
                                            previous_it->second.write_bytes) /
                        elapsed;
                }
            }

            samples[block.name] = {counters->read_bytes, counters->write_bytes, now};

            device.read_bytes_total += block.read_bytes_total;
            device.write_bytes_total += block.write_bytes_total;
            device.read_bps += block.read_bps;
            device.write_bps += block.write_bps;
        }
    }
}

void enrich_active_devices(StatusSnapshot &status,
                           std::unordered_map<std::string, BlockIoSample> &samples) {
    const auto mounts = parse_mounts();

    for (auto &device : status.active_devices) {
        enrich_authorization(device);
        const auto block_names = find_usb_block_devices(device.devpath);
        for (const auto &block_name : block_names) {
            device.block_devices.push_back(collect_block_info(block_name, mounts));
        }
    }

    update_io_metrics(status, samples);
}

std::string normalized_policy_mode(const StatusSnapshot &status) {
    if (!status.policy_mode.empty()) {
        return status.policy_mode;
    }
    return status.whitelist_enabled ? "whitelist" : "observe";
}

bool whitelist_policy_active(const StatusSnapshot &status) {
    return normalized_policy_mode(status) == "whitelist";
}

std::string policy_mode_label(const StatusSnapshot &status) {
    return whitelist_policy_active(status) ? "Whitelist" : "Giám sát";
}

std::string device_trust_label(const StatusSnapshot &status, bool whitelisted) {
    if (!whitelist_policy_active(status)) {
        return "Chỉ giám sát";
    }
    return whitelisted ? "Tin cậy" : "Ngoài whitelist";
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

std::string format_rate(double bytes_per_second) {
    if (bytes_per_second <= 0.0) {
        return "0 B/s";
    }
    return format_size(static_cast<std::uint64_t>(bytes_per_second)) + "/s";
}

std::string join_strings(const std::vector<std::string> &items,
                         const std::string &delimiter) {
    std::ostringstream out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) {
            out << delimiter;
        }
        out << items[i];
    }
    return out.str();
}

std::vector<std::string> collect_mounts(const ActiveDevice &device) {
    std::set<std::string> mounts;
    for (const auto &block : device.block_devices) {
        mounts.insert(block.mountpoints.begin(), block.mountpoints.end());
        for (const auto &part : block.partitions) {
            mounts.insert(part.mountpoints.begin(), part.mountpoints.end());
        }
    }
    return std::vector<std::string>(mounts.begin(), mounts.end());
}

std::string summarize_mounts(const ActiveDevice &device) {
    const auto mounts = collect_mounts(device);
    return mounts.empty() ? "(không)" : join_strings(mounts, ", ");
}

unsigned int count_active_by_trust(const StatusSnapshot &status, bool whitelisted) {
    return static_cast<unsigned int>(std::count_if(
        status.active_devices.begin(), status.active_devices.end(),
        [whitelisted](const ActiveDevice &device) {
            return device.whitelisted == whitelisted;
        }));
}

StatusSnapshot load_status(const fs::path &status_file,
                           std::unordered_map<std::string, BlockIoSample> &samples,
                           bool enrich) {
    if (!fs::exists(status_file)) {
        throw std::runtime_error("Không tìm thấy " + status_file.string());
    }

    StatusSnapshot status = parse_status_text(read_text_file(status_file));
    status.policy_mode = normalized_policy_mode(status);
    if (enrich) {
        enrich_active_devices(status, samples);
    }
    return status;
}

std::string timestamp_now() {
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm time_info{};
    localtime_r(&now, &time_info);

    std::ostringstream out;
    out << std::put_time(&time_info, "%F %T");
    return out.str();
}

std::string selected_device_key(const ActiveDevice &device) {
    return device.devpath + "|" + device.vid + ":" + device.pid + "|" + device.serial;
}

std::string selected_device_label(const ActiveDevice &device) {
    std::ostringstream out;
    out << device.devpath << " (" << device.vid << ':' << device.pid << ')';
    if (!device.product.empty()) {
        out << " - " << device.product;
    }
    return out.str();
}

fs::path control_file_from_status(const fs::path &status_file) {
    return status_file.parent_path() / "control";
}

bool has_real_value(const std::string &value) {
    return !value.empty() && value != kUnknownValue;
}

std::string persistent_device_key(const ActiveDevice &device) {
    std::ostringstream key;
    key << device.vid << ':' << device.pid;
    if (has_real_value(device.serial)) {
        key << ':' << device.serial;
    }
    return key.str();
}

SavedAccessMode desired_access_mode_for_device(const PersistentConfig &config,
                                               const ActiveDevice &device) {
    const auto it = config.access_policies.find(persistent_device_key(device));
    if (it == config.access_policies.end()) {
        return SavedAccessMode::None;
    }
    return it->second;
}

std::string authorization_label(const ActiveDevice &device) {
    if (!device.authorization_supported) {
        return "Không xác định";
    }
    return device.authorized ? "Đang cho phép" : "Đã chặn";
}

enum class AccessModeSummary {
    Unknown = 0,
    ReadWrite = 1,
    ReadOnly = 2,
    Mixed = 3,
};

AccessModeSummary summarize_access_mode(const ActiveDevice &device) {
    int readonly_count = 0;
    int readwrite_count = 0;

    for (const auto &block : device.block_devices) {
        block.read_only ? ++readonly_count : ++readwrite_count;
        for (const auto &part : block.partitions) {
            part.read_only ? ++readonly_count : ++readwrite_count;
        }
    }

    if (readonly_count == 0 && readwrite_count == 0) {
        return AccessModeSummary::Unknown;
    }
    if (readonly_count > 0 && readwrite_count == 0) {
        return AccessModeSummary::ReadOnly;
    }
    if (readwrite_count > 0 && readonly_count == 0) {
        return AccessModeSummary::ReadWrite;
    }
    return AccessModeSummary::Mixed;
}

std::string access_mode_label(const ActiveDevice &device) {
    switch (summarize_access_mode(device)) {
        case AccessModeSummary::ReadOnly:
            return "Chỉ đọc";
        case AccessModeSummary::ReadWrite:
            return "Đọc/ghi";
        case AccessModeSummary::Mixed:
            return "Hỗn hợp";
        case AccessModeSummary::Unknown:
        default:
            return "Không xác định";
    }
}

std::string block_access_label(bool readonly) {
    return readonly ? "chỉ đọc" : "đọc/ghi";
}

std::vector<std::string> collect_block_node_names(const ActiveDevice &device) {
    std::vector<std::string> names;
    std::set<std::string> seen;

    for (const auto &block : device.block_devices) {
        if (seen.insert(block.name).second) {
            names.push_back(block.name);
        }
        for (const auto &part : block.partitions) {
            if (seen.insert(part.name).second) {
                names.push_back(part.name);
            }
        }
    }

    return names;
}

std::optional<std::string> remount_mountpoint(const std::string &mountpoint,
                                              bool readonly) {
    const unsigned long flags =
        static_cast<unsigned long>(MS_REMOUNT) |
        (readonly ? static_cast<unsigned long>(MS_RDONLY) : 0UL);

    if (::mount(nullptr, mountpoint.c_str(), nullptr, flags, nullptr) == 0) {
        return std::nullopt;
    }

    return "Lỗi: remount " + mountpoint + " sang chế độ " +
           block_access_label(readonly) + " thất bại: " +
           std::strerror(errno) + '.';
}

std::optional<std::string> write_block_ro_sysfs(const std::string &block_name,
                                                bool readonly) {
    const fs::path ro_path = kSysClassBlock / block_name / "ro";
    std::ofstream output(ro_path);
    if (!output) {
        return "Lỗi: không thể mở " + ro_path.string() + '.';
    }

    output << (readonly ? "1\n" : "0\n");
    if (!output) {
        return "Lỗi: ghi thất bại vào " + ro_path.string() + '.';
    }

    return std::nullopt;
}

std::optional<std::string> set_block_node_access(const std::string &block_name,
                                                 bool readonly) {
    const std::string device_node = "/dev/" + block_name;
    const int desired = readonly ? 1 : 0;
    int saved_errno = 0;
    int fd = ::open(device_node.c_str(), O_RDONLY | O_CLOEXEC);

    if (fd >= 0) {
        if (::ioctl(fd, BLKROSET, &desired) == 0) {
            ::close(fd);
            return std::nullopt;
        }
        saved_errno = errno;
        ::close(fd);
    } else {
        saved_errno = errno;
    }

    const auto sysfs_error = write_block_ro_sysfs(block_name, readonly);
    if (!sysfs_error.has_value()) {
        return std::nullopt;
    }

    std::ostringstream error;
    error << "Lỗi: không thể đặt " << device_node << " sang chế độ "
          << block_access_label(readonly) << " ("
          << std::strerror(saved_errno) << "). " << *sysfs_error;
    return error.str();
}

std::vector<std::string> apply_access_mode_to_device(const ActiveDevice &device,
                                                     bool readonly) {
    std::vector<std::string> messages;
    const auto mountpoints = collect_mounts(device);
    const auto block_names = collect_block_node_names(device);
    bool any_success = false;

    if (!device.authorized) {
        messages.push_back("Cảnh báo: USB " + device.devpath +
                           " đang ở trạng thái đã chặn, không thể đổi quyền truy cập.");
        return messages;
    }

    if (block_names.empty()) {
        messages.push_back("Cảnh báo: chưa phân giải được block device cho USB " +
                           device.devpath + '.');
        return messages;
    }

    ::sync();

    auto apply_mounts = [&](bool ro_mode) {
        for (const auto &mountpoint : mountpoints) {
            const auto error = remount_mountpoint(mountpoint, ro_mode);
            if (error.has_value()) {
                messages.push_back(*error);
            } else {
                any_success = true;
                messages.push_back("Đã remount " + mountpoint + " sang chế độ " +
                                   block_access_label(ro_mode) + '.');
            }
        }
    };

    auto apply_blocks = [&](bool ro_mode) {
        for (const auto &block_name : block_names) {
            const auto error = set_block_node_access(block_name, ro_mode);
            if (error.has_value()) {
                messages.push_back(*error);
            } else {
                any_success = true;
                messages.push_back("Đã đặt /dev/" + block_name + " sang chế độ " +
                                   block_access_label(ro_mode) + '.');
            }
        }
    };

    if (readonly) {
        apply_mounts(true);
        apply_blocks(true);
    } else {
        apply_blocks(false);
        apply_mounts(false);
    }

    if (!any_success && messages.empty()) {
        messages.push_back("Không có thay đổi nào được áp dụng cho USB " +
                           device.devpath + '.');
    }

    return messages;
}

void append_action_logs(GuiState *state, const std::vector<std::string> &messages) {
    for (const auto &message : messages) {
        if (starts_with(message, "Lỗi") || starts_with(message, "Cảnh báo")) {
            append_log(state, "CẢNH BÁO", message);
        } else if (starts_with(message, "Đã ")) {
            append_log(state, "CHÍNH SÁCH", message);
        } else {
            append_log(state, "THÔNG TIN", message);
        }
    }
}

const ActiveDevice *get_selected_active_device(GuiState *state) {
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(state->active_tree));
    GtkTreeModel *model = nullptr;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return nullptr;
    }

    int index = 0;
    gtk_tree_model_get(model, &iter, ACTIVE_COL_INDEX, &index, -1);
    if (index < 0 ||
        static_cast<std::size_t>(index) >= state->current_status.active_devices.size()) {
        return nullptr;
    }

    return &state->current_status.active_devices[static_cast<std::size_t>(index)];
}

std::string whitelist_rule_for_device(const ActiveDevice *device) {
    if (!device) {
        return "";
    }

    std::ostringstream rule;
    rule << device->vid << ':' << device->pid;
    if (has_real_value(device->serial)) {
        rule << ':' << device->serial;
    }
    return rule.str();
}

std::vector<std::string> split_whitelist_entries(const std::string &entries) {
    std::vector<std::string> rules;
    const std::string normalized = trim(entries);

    if (normalized.empty() || normalized == "(none)") {
        return rules;
    }

    std::istringstream input(normalized);
    std::string token;
    while (std::getline(input, token, ',')) {
        token = trim(token);
        if (!token.empty()) {
            rules.push_back(token);
        }
    }

    return rules;
}

std::vector<std::string> normalize_rules(std::vector<std::string> rules) {
    for (auto &rule : rules) {
        rule = trim(rule);
    }

    rules.erase(std::remove_if(rules.begin(), rules.end(),
                               [](const std::string &rule) { return rule.empty(); }),
                rules.end());
    std::sort(rules.begin(), rules.end());
    rules.erase(std::unique(rules.begin(), rules.end()), rules.end());
    return rules;
}

PersistentConfig load_persistent_config(const fs::path &config_file) {
    PersistentConfig config;
    std::ifstream input(config_file);
    std::string line;

    if (!input) {
        return config;
    }

    while (std::getline(input, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const auto fields = split_tab_fields(line);
        if (fields.empty()) {
            continue;
        }

        if (fields[0] == "whitelist" && fields.size() >= 2) {
            config.whitelist_rules.push_back(trim(fields[1]));
            continue;
        }

        if (fields[0] == "access" && fields.size() >= 3) {
            const std::string key = trim(fields[1]);
            const SavedAccessMode mode = parse_saved_access_mode(fields[2]);
            if (!key.empty() && mode != SavedAccessMode::None) {
                config.access_policies[key] = mode;
            }
        }
    }

    config.whitelist_rules = normalize_rules(std::move(config.whitelist_rules));
    return config;
}

void save_persistent_config_or_throw(const fs::path &config_file,
                                     const PersistentConfig &config) {
    std::error_code ec;
    if (!config_file.parent_path().empty()) {
        fs::create_directories(config_file.parent_path(), ec);
        if (ec) {
            throw std::runtime_error("Không thể tạo thư mục cấu hình " +
                                     config_file.parent_path().string() + '.');
        }
    }

    std::ofstream output(config_file);
    if (!output) {
        throw std::runtime_error("Không thể mở " + config_file.string() +
                                 " để lưu cấu hình.");
    }

    output << "# usb_guard persistent config\n";
    for (const auto &rule : normalize_rules(config.whitelist_rules)) {
        output << "whitelist\t" << escape_config_field(rule) << '\n';
    }

    for (const auto &[key, mode] : config.access_policies) {
        if (mode == SavedAccessMode::None) {
            continue;
        }
        output << "access\t" << escape_config_field(key) << '\t'
               << saved_access_mode_to_string(mode) << '\n';
    }

    if (!output) {
        throw std::runtime_error("Ghi thất bại vào " + config_file.string() +
                                 " khi lưu cấu hình.");
    }
}

std::optional<std::string> prompt_whitelist_rule_dialog(GtkWindow *parent,
                                                        const std::string &title,
                                                        const std::string &description,
                                                        const std::string &initial_rule) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title.c_str(), parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Hủy", GTK_RESPONSE_CANCEL,
        "_Xác nhận", GTK_RESPONSE_OK, nullptr);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *label = gtk_label_new(description.c_str());
    GtkWidget *entry = gtk_entry_new();
    std::optional<std::string> result;

    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(entry), "Ví dụ: 346d:5678 hoặc 346d:5678:SERIAL");
    gtk_entry_set_text(GTK_ENTRY(entry), initial_rule.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const std::string value =
            trim(gtk_entry_get_text(GTK_ENTRY(entry)));
        if (!value.empty()) {
            result = value;
        }
    }

    gtk_widget_destroy(dialog);
    return result;
}

std::optional<std::string> prompt_select_whitelist_rule_dialog(
    GtkWindow *parent, const std::vector<std::string> &rules,
    const std::string &preferred_rule) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Xóa mục khỏi whitelist", parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Hủy", GTK_RESPONSE_CANCEL,
        "_Xóa", GTK_RESPONSE_OK, nullptr);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *label = gtk_label_new(
        "Chọn mục whitelist cần xóa trong danh sách hiện tại.");
    GtkWidget *combo = gtk_combo_box_text_new();
    std::optional<std::string> result;
    int active_index = 0;

    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    for (std::size_t i = 0; i < rules.size(); ++i) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                       rules[i].c_str());
        if (!preferred_rule.empty() && rules[i] == preferred_rule) {
            active_index = static_cast<int>(i);
        }
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active_index);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gchar *selected =
            gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
        if (selected != nullptr) {
            result = trim(selected);
            g_free(selected);
        }
    }

    gtk_widget_destroy(dialog);
    return result;
}

void write_control_command_or_throw(const fs::path &control_file,
                                    const std::string &command) {
    std::ofstream output(control_file);
    if (!output) {
        throw std::runtime_error("Không thể mở " + control_file.string() +
                                 " để gửi lệnh điều khiển whitelist.");
    }

    output << command << '\n';
    if (!output) {
        throw std::runtime_error("Ghi thất bại vào " + control_file.string() +
                                 " khi cập nhật whitelist.");
    }
}

void rebuild_log_buffer(GuiState *state) {
    std::ostringstream content;
    for (const auto &line : state->log_lines) {
        content << line << '\n';
    }
    gtk_text_buffer_set_text(state->log_buffer, content.str().c_str(), -1);
}

void append_log(GuiState *state, const std::string &level,
                const std::string &message) {
    std::ostringstream line;
    line << '[' << timestamp_now() << "] [" << level << "] " << message;
    state->log_lines.push_back(line.str());
    while (state->log_lines.size() > kMaxLogLines) {
        state->log_lines.pop_front();
    }
    rebuild_log_buffer(state);
}

void persist_whitelist_from_status(GuiState *state) {
    state->persistent_config.whitelist_rules =
        normalize_rules(split_whitelist_entries(state->current_status.whitelist_entries));
}

void save_gui_config_or_throw(GuiState *state) {
    save_persistent_config_or_throw(state->options.config_file,
                                    state->persistent_config);
    state->persistent_config_present = true;
}

bool sync_runtime_whitelist_from_config_or_throw(GuiState *state,
                                                 const StatusSnapshot &status) {
    const auto configured = normalize_rules(state->persistent_config.whitelist_rules);
    const auto current =
        normalize_rules(split_whitelist_entries(status.whitelist_entries));

    if (configured == current) {
        return false;
    }

    if (geteuid() != 0) {
        if (!state->whitelist_sync_warning_emitted) {
            append_log(state, "CẢNH BÁO",
                       "Không thể đồng bộ whitelist đã lưu vì GUI không chạy bằng quyền root.");
            state->whitelist_sync_warning_emitted = true;
        }
        return false;
    }

    const fs::path control_file = control_file_from_status(state->options.status_file);
    write_control_command_or_throw(control_file, "clear");
    for (const auto &rule : configured) {
        write_control_command_or_throw(control_file, "add " + rule);
    }

    append_log(state, "CHÍNH SÁCH",
               "Đã đồng bộ whitelist đã lưu vào kernel module.");
    return true;
}

std::vector<std::string> apply_saved_access_policies(GuiState *state,
                                                     const StatusSnapshot &status) {
    std::vector<std::string> actions;
    bool has_saved_policy = false;

    for (const auto &device : status.active_devices) {
        const SavedAccessMode desired =
            desired_access_mode_for_device(state->persistent_config, device);
        if (desired == SavedAccessMode::None) {
            continue;
        }

        has_saved_policy = true;
        if (geteuid() != 0) {
            continue;
        }
        if (!device.authorized || device.block_devices.empty()) {
            continue;
        }

        const AccessModeSummary current = summarize_access_mode(device);
        if ((desired == SavedAccessMode::ReadOnly &&
             current == AccessModeSummary::ReadOnly) ||
            (desired == SavedAccessMode::ReadWrite &&
             current == AccessModeSummary::ReadWrite)) {
            continue;
        }

        const auto device_actions =
            apply_access_mode_to_device(device,
                                        desired == SavedAccessMode::ReadOnly);
        actions.insert(actions.end(), device_actions.begin(), device_actions.end());
    }

    if (has_saved_policy && geteuid() != 0 && actions.empty()) {
        if (!state->access_policy_warning_emitted) {
            append_log(state, "CẢNH BÁO",
                       "Không thể tự áp lại cấu hình truy cập đã lưu vì GUI không chạy bằng quyền root.");
            state->access_policy_warning_emitted = true;
        }
    }

    return actions;
}

void set_status_text(GuiState *state, const std::string &text) {
    gtk_label_set_text(GTK_LABEL(state->status_label), text.c_str());
}

void set_banner_markup(GuiState *state, const std::string &markup) {
    gtk_label_set_markup(GTK_LABEL(state->banner_label), markup.c_str());
}

void show_message_dialog(GtkWindow *parent, GtkMessageType type,
                         const std::string &title, const std::string &message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT),
        type, GTK_BUTTONS_OK, "%s", title.c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
                                             message.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void update_summary_labels(GuiState *state, const StatusSnapshot &status) {
    const unsigned int trusted_count = count_active_by_trust(status, true);
    const unsigned int untrusted_count = count_active_by_trust(status, false);

    gtk_label_set_text(GTK_LABEL(state->module_value),
                       status.module.empty() ? "usb_guard" : status.module.c_str());
    gtk_label_set_text(GTK_LABEL(state->mode_value),
                       policy_mode_label(status).c_str());
    gtk_label_set_text(GTK_LABEL(state->whitelist_value),
                       status.whitelist_entries.empty() ? "(không có)"
                                                        : status.whitelist_entries.c_str());
    gtk_label_set_text(GTK_LABEL(state->event_value), status.last_event.c_str());

    const std::string active_text = std::to_string(status.active_count);
    const std::string history_text = std::to_string(status.history_count);
    const std::string trusted_text = std::to_string(trusted_count);
    const std::string untrusted_text =
        whitelist_policy_active(status) ? std::to_string(untrusted_count) : "0";

    gtk_label_set_text(GTK_LABEL(state->active_value), active_text.c_str());
    gtk_label_set_text(GTK_LABEL(state->history_value), history_text.c_str());
    gtk_label_set_text(GTK_LABEL(state->trusted_value), trusted_text.c_str());
    gtk_label_set_text(GTK_LABEL(state->untrusted_value), untrusted_text.c_str());

    if (status.active_devices.empty()) {
        set_banner_markup(
            state,
            "<span size='large' weight='bold'>Hệ thống đang theo dõi ổ USB.</span>\n"
            "Chưa phát hiện USB mass storage nào đang kết nối.");
        return;
    }

    if (whitelist_policy_active(status) && untrusted_count > 0) {
        std::ostringstream markup;
        markup << "<span size='large' weight='bold' foreground='#c0392b'>Cảnh báo: phát hiện "
               << untrusted_count
               << " USB ngoài whitelist.</span>\n"
               << "Theo dõi trạng thái `Đã chặn` để biết thiết bị có bị kernel module chặn tự động hay không.";
        set_banner_markup(state, markup.str());
        return;
    }

    if (whitelist_policy_active(status)) {
        set_banner_markup(
            state,
            "<span size='large' weight='bold' foreground='#1f8f4a'>Tất cả USB hiện tại đều nằm trong whitelist.</span>\n"
            "usb_guard vẫn chỉ giám sát, không thay thế usb-storage của hệ điều hành.");
        return;
    }

    set_banner_markup(
        state,
        "<span size='large' weight='bold' foreground='#355c7d'>Chế độ giám sát đang bật.</span>\n"
        "Whitelist chưa được cấu hình, mọi USB được theo dõi nhưng chưa bị đánh dấu chặn.");
}

void append_text_column(GtkWidget *tree, const char *title, gint text_col,
                        gint color_col = -1, gboolean expand = FALSE) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, title);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", text_col);
    if (color_col >= 0) {
        gtk_tree_view_column_add_attribute(column, renderer, "foreground", color_col);
    }
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_expand(column, expand);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
}

void populate_active_store(GuiState *state) {
    gtk_list_store_clear(state->active_store);

    for (std::size_t i = 0; i < state->current_status.active_devices.size(); ++i) {
        const auto &device = state->current_status.active_devices[i];
        GtkTreeIter iter;
        const std::string mounts = summarize_mounts(device);
        std::string trust = device_trust_label(state->current_status,
                                               device.whitelisted);
        const std::string device_label =
            device.devpath + " | " + device.vid + ":" + device.pid;
        const std::string product =
            device.manufacturer.empty() ? device.product
                                        : device.manufacturer + " / " + device.product;
        const std::string access_mode = access_mode_label(device);
        const std::string io_rate =
            "R " + format_rate(device.read_bps) + " | W " + format_rate(device.write_bps);
        const std::string io_total =
            "R " + format_size(device.read_bytes_total) + " | W " +
            format_size(device.write_bytes_total);
        std::string trust_color =
            !whitelist_policy_active(state->current_status) ? "#7f8c8d"
            : device.whitelisted ? "#1f8f4a"
                                 : "#c0392b";

        if (device.authorization_supported && !device.authorized) {
            trust += " / Đã chặn";
            trust_color = "#9b1c1c";
        }

        gtk_list_store_append(state->active_store, &iter);
        gtk_list_store_set(
            state->active_store, &iter,
            ACTIVE_COL_INDEX, static_cast<int>(i),
            ACTIVE_COL_DEVICE, device_label.c_str(),
            ACTIVE_COL_TRUST, trust.c_str(),
            ACTIVE_COL_ACCESS, access_mode.c_str(),
            ACTIVE_COL_PRODUCT, product.c_str(),
            ACTIVE_COL_IO_RATE, io_rate.c_str(),
            ACTIVE_COL_IO_TOTAL, io_total.c_str(),
            ACTIVE_COL_MOUNTS, mounts.c_str(),
            ACTIVE_COL_TRUST_COLOR, trust_color.c_str(), -1);
    }
}

void populate_history_store(GuiState *state) {
    gtk_list_store_clear(state->history_store);

    for (const auto &entry : state->current_status.history_devices) {
        GtkTreeIter iter;
        const std::string vidpid = entry.vid + ":" + entry.pid;
        const std::string connect_count = std::to_string(entry.connect_count);
        const std::string active_instances = std::to_string(entry.active_instances);
        const std::string total_duration =
            format_duration_ms(entry.total_connected_ms);

        gtk_list_store_append(state->history_store, &iter);
        gtk_list_store_set(state->history_store, &iter,
                           HISTORY_COL_VIDPID, vidpid.c_str(),
                           HISTORY_COL_SERIAL, entry.serial.c_str(),
                           HISTORY_COL_CONNECT_COUNT, connect_count.c_str(),
                           HISTORY_COL_ACTIVE_INSTANCES, active_instances.c_str(),
                           HISTORY_COL_TOTAL_DURATION, total_duration.c_str(), -1);
    }
}

std::string current_selected_key(GuiState *state) {
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(state->active_tree));
    GtkTreeModel *model = nullptr;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return "";
    }

    int index = 0;
    gtk_tree_model_get(model, &iter, ACTIVE_COL_INDEX, &index, -1);
    if (index < 0 ||
        static_cast<std::size_t>(index) >= state->current_status.active_devices.size()) {
        return "";
    }

    return selected_device_key(
        state->current_status.active_devices[static_cast<std::size_t>(index)]);
}

void restore_active_selection(GuiState *state, const std::string &wanted_key) {
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(state->active_tree));
    GtkTreeModel *model = GTK_TREE_MODEL(state->active_store);
    GtkTreeIter iter;

    if (!gtk_tree_model_get_iter_first(model, &iter)) {
        gtk_text_buffer_set_text(
            state->detail_buffer,
            "Chọn một USB trong danh sách để xem thông tin chi tiết.\n", -1);
        return;
    }

    GtkTreeIter first_iter = iter;
    bool found = false;

    do {
        int index = 0;
        gtk_tree_model_get(model, &iter, ACTIVE_COL_INDEX, &index, -1);
        if (index >= 0 &&
            static_cast<std::size_t>(index) < state->current_status.active_devices.size()) {
            const auto &device =
                state->current_status.active_devices[static_cast<std::size_t>(index)];
            if (!wanted_key.empty() && selected_device_key(device) == wanted_key) {
                gtk_tree_selection_select_iter(selection, &iter);
                found = true;
                break;
            }
        }
    } while (gtk_tree_model_iter_next(model, &iter));

    if (!found) {
        gtk_tree_selection_select_iter(selection, &first_iter);
    }
}

void update_detail_text(GuiState *state) {
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(state->active_tree));
    GtkTreeModel *model = nullptr;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        update_access_action_buttons(state);
        gtk_text_buffer_set_text(
            state->detail_buffer,
            "Chọn một USB trong danh sách để xem thông tin chi tiết.\n", -1);
        return;
    }

    int index = 0;
    gtk_tree_model_get(model, &iter, ACTIVE_COL_INDEX, &index, -1);
    if (index < 0 ||
        static_cast<std::size_t>(index) >= state->current_status.active_devices.size()) {
        update_access_action_buttons(state);
        gtk_text_buffer_set_text(state->detail_buffer, "Không có dữ liệu chi tiết.\n",
                                 -1);
        return;
    }

    const auto &device =
        state->current_status.active_devices[static_cast<std::size_t>(index)];

    std::ostringstream detail;
    detail << "TÓM TẮT USB\n";
    detail << "-----------\n";
    detail << "Đường dẫn: " << device.devpath << '\n';
    detail << "Bus/Dev: " << device.bus << '/' << device.dev << '\n';
    detail << "VID:PID: " << device.vid << ':' << device.pid << '\n';
    detail << "Trạng thái: "
           << device_trust_label(state->current_status, device.whitelisted) << '\n';
    detail << "Quyền truy cập: " << access_mode_label(device) << '\n';
    detail << "Chính sách lưu: "
           << saved_access_mode_label(
                  desired_access_mode_for_device(state->persistent_config, device))
           << '\n';
    detail << "Ủy quyền USB: " << authorization_label(device) << '\n';
    detail << "Hãng sản xuất: " << device.manufacturer << '\n';
    detail << "Sản phẩm: " << device.product << '\n';
    detail << "Số serial: " << device.serial << '\n';
    detail << "Số lần cắm: " << device.connect_count << '\n';
    detail << "Đang kết nối: " << format_duration_ms(device.connected_for_ms) << '\n';
    detail << "Tổng thời gian: " << format_duration_ms(device.total_connected_ms) << '\n';
    detail << "Lưu lượng đọc: " << format_size(device.read_bytes_total)
           << " | tốc độ hiện tại: " << format_rate(device.read_bps) << '\n';
    detail << "Lưu lượng ghi: " << format_size(device.write_bytes_total)
           << " | tốc độ hiện tại: " << format_rate(device.write_bps) << '\n';
    detail << '\n';
    detail << "BLOCK DEVICE\n";
    detail << "------------\n";

    if (device.block_devices.empty()) {
        detail << "Chưa phân giải được block device cho USB này.\n";
    } else {
        for (const auto &block : device.block_devices) {
            detail << "* " << block.name << " | dung lượng "
                   << format_size(block.size_bytes)
                   << " | truy cập " << block_access_label(block.read_only) << '\n';
            detail << "  Mountpoint: "
                   << (block.mountpoints.empty() ? "(không)"
                                                 : join_strings(block.mountpoints, ", "))
                   << '\n';
            detail << "  Tổng đọc: " << format_size(block.read_bytes_total)
                   << " | tốc độ: " << format_rate(block.read_bps) << '\n';
            detail << "  Tổng ghi: " << format_size(block.write_bytes_total)
                   << " | tốc độ: " << format_rate(block.write_bps) << '\n';

            if (!block.partitions.empty()) {
                detail << "  Phân vùng:\n";
                for (const auto &part : block.partitions) {
                    detail << "    - " << part.name << " | dung lượng "
                           << format_size(part.size_bytes)
                           << " | truy cập " << block_access_label(part.read_only)
                           << " | mount "
                           << (part.mountpoints.empty() ? "(không)"
                                                        : join_strings(part.mountpoints, ", "))
                           << '\n';
                }
            }
            detail << '\n';
        }
    }

    detail << "Ghi chú: tốc độ đọc/ghi ở đây được ước lượng từ thống kê block layer,\n"
           << "không phải tốc độ tuyệt đối trên dây USB.\n";

    update_access_action_buttons(state);
    gtk_text_buffer_set_text(state->detail_buffer, detail.str().c_str(), -1);
}

void on_active_selection_changed(GtkTreeSelection *, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    update_detail_text(state);
}

void log_device_changes(GuiState *state, const StatusSnapshot &status) {
    std::map<std::string, std::string> current_labels;

    for (const auto &device : status.active_devices) {
        current_labels[selected_device_key(device)] = selected_device_label(device);
    }

    if (!state->first_refresh_done) {
        std::ostringstream info;
        info << "Đã tải trạng thái ban đầu từ " << state->options.status_file.string()
             << ". Số USB đang kết nối: " << status.active_count << '.';
        append_log(state, "THÔNG TIN", info.str());
        state->previous_active_labels = std::move(current_labels);
        state->first_refresh_done = true;
        return;
    }

    for (const auto &pair : current_labels) {
        if (state->previous_active_labels.find(pair.first) ==
            state->previous_active_labels.end()) {
            append_log(state, "SỰ KIỆN",
                       "Phát hiện USB mới: " + pair.second + '.');

            const auto device_it = std::find_if(
                status.active_devices.begin(), status.active_devices.end(),
                [&](const ActiveDevice &device) {
                    return selected_device_key(device) == pair.first;
                });
            if (device_it != status.active_devices.end() &&
                whitelist_policy_active(status) && !device_it->whitelisted) {
                append_log(state, "CẢNH BÁO",
                           "USB ngoài whitelist: " + pair.second +
                               ". Theo dõi trạng thái `Đã chặn` để kiểm tra việc chặn tự động ở kernel.");
            }
        }
    }

    for (const auto &pair : state->previous_active_labels) {
        if (current_labels.find(pair.first) == current_labels.end()) {
            append_log(state, "SỰ KIỆN", "USB đã rút ra: " + pair.second + '.');
        }
    }

    state->previous_active_labels = std::move(current_labels);
}

gboolean on_refresh_timer(gpointer user_data);

void restart_refresh_timer(GuiState *state) {
    if (state->refresh_source_id != 0U) {
        g_source_remove(state->refresh_source_id);
        state->refresh_source_id = 0U;
    }

    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->auto_refresh_check))) {
        return;
    }

    const double seconds =
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(state->interval_spin));
    const guint interval_ms =
        static_cast<guint>(std::max(0.2, seconds) * 1000.0);
    state->refresh_source_id = g_timeout_add(interval_ms, on_refresh_timer, state);
}

void set_refresh_status(GuiState *state, const std::string &prefix) {
    std::ostringstream text;
    text << prefix << " | Tệp trạng thái: " << state->options.status_file.string()
         << " | Cập nhật lúc " << timestamp_now();
    set_status_text(state, text.str());
}

void refresh_status_view(GuiState *state, bool log_on_success) {
    const std::string selected_key = current_selected_key(state);

    try {
        if (!state->persistent_config_loaded) {
            state->persistent_config_present = fs::exists(state->options.config_file);
            state->persistent_config = load_persistent_config(state->options.config_file);
            state->persistent_config_loaded = true;
        }

        StatusSnapshot status =
            load_status(state->options.status_file, state->io_samples, true);

        if (state->persistent_config_present && !state->whitelist_sync_attempted) {
            state->whitelist_sync_attempted = true;
            if (sync_runtime_whitelist_from_config_or_throw(state, status)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                status = load_status(state->options.status_file, state->io_samples,
                                     true);
            }
        }

        const auto access_actions = apply_saved_access_policies(state, status);
        const bool access_changed = std::any_of(
            access_actions.begin(), access_actions.end(),
            [](const std::string &message) { return starts_with(message, "Đã "); });
        if (!access_actions.empty()) {
            append_action_logs(state, access_actions);
            if (access_changed) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                status = load_status(state->options.status_file, state->io_samples,
                                     true);
            }
        }

        log_device_changes(state, status);
        state->current_status = std::move(status);
        state->last_error.clear();

        update_summary_labels(state, state->current_status);
        populate_active_store(state);
        populate_history_store(state);
        restore_active_selection(state, selected_key);
        update_detail_text(state);
        update_access_action_buttons(state);
        set_refresh_status(state, "Theo dõi USB ổn định");

        if (log_on_success) {
            append_log(state, "THÔNG TIN",
                       "Đã làm mới dữ liệu giám sát USB.");
        }
    } catch (const std::exception &ex) {
        if (state->last_error != ex.what()) {
            append_log(state, "LỖI", ex.what());
            state->last_error = ex.what();
        }

        set_banner_markup(
            state,
            "<span size='large' weight='bold' foreground='#c0392b'>Không đọc được trạng thái từ usb_guard.</span>\n"
            "Hãy kiểm tra module đã được nạp và /proc/usb_guard/status có tồn tại.");
        set_status_text(
            state,
            ("Lỗi: " + std::string(ex.what()) +
             " | Hãy nạp module usb_guard trước khi mở giao diện.")
                .c_str());
        gtk_text_buffer_set_text(
            state->detail_buffer,
            "Không thể tải dữ liệu từ /proc/usb_guard/status.\n"
            "Kiểm tra lại việc biên dịch, nạp module và quyền truy cập.\n",
            -1);
        update_access_action_buttons(state);
        gtk_list_store_clear(state->active_store);
        gtk_list_store_clear(state->history_store);
    }
}

gboolean on_refresh_timer(gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    refresh_status_view(state, false);
    return G_SOURCE_CONTINUE;
}

void on_refresh_clicked(GtkButton *, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    refresh_status_view(state, true);
}

void on_auto_refresh_toggled(GtkToggleButton *, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    restart_refresh_timer(state);
}

void on_interval_changed(GtkSpinButton *, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    restart_refresh_timer(state);
}

void update_access_action_buttons(GuiState *state) {
    bool sensitive = false;

    if (state->active_tree != nullptr) {
        const ActiveDevice *device = get_selected_active_device(state);
        sensitive =
            device != nullptr && device->authorized && !device->block_devices.empty();
    }

    if (state->readonly_button) {
        gtk_widget_set_sensitive(state->readonly_button, sensitive);
    }
    if (state->readwrite_button) {
        gtk_widget_set_sensitive(state->readwrite_button, sensitive);
    }
}

void on_clear_log_clicked(GtkButton *, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    state->log_lines.clear();
    rebuild_log_buffer(state);
    append_log(state, "THÔNG TIN", "Đã xóa nhật ký trên giao diện.");
}

void perform_access_mode_action(GuiState *state, bool readonly) {
    const ActiveDevice *device = get_selected_active_device(state);
    const std::string mode_label = readonly ? "chỉ đọc" : "đọc/ghi";

    if (device == nullptr) {
        show_message_dialog(GTK_WINDOW(state->window), GTK_MESSAGE_INFO,
                            "Chưa chọn USB",
                            "Hãy chọn một USB trong danh sách trước khi đổi quyền truy cập.");
        return;
    }

    try {
        if (geteuid() != 0) {
            throw std::runtime_error(
                "Cần chạy giao diện bằng quyền root để đổi quyền truy cập cho USB.");
        }

        state->persistent_config_loaded = true;
        state->persistent_config.access_policies[persistent_device_key(*device)] =
            readonly ? SavedAccessMode::ReadOnly : SavedAccessMode::ReadWrite;
        save_gui_config_or_throw(state);

        const auto actions = apply_access_mode_to_device(*device, readonly);
        append_action_logs(state, actions);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        refresh_status_view(state, false);

        const bool has_errors = std::any_of(
            actions.begin(), actions.end(), [](const std::string &message) {
                return starts_with(message, "Lỗi");
            });

        const GtkMessageType type = has_errors ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO;
        const std::string title =
            has_errors ? "Đổi quyền truy cập hoàn tất một phần"
                       : "Đổi quyền truy cập thành công";
        const std::string message =
            "USB đã được xử lý sang chế độ " + mode_label +
            ". Xem tab Nhật ký để biết chi tiết từng block device và mountpoint.";
        show_message_dialog(GTK_WINDOW(state->window), type, title, message);
    } catch (const std::exception &ex) {
        append_log(state, "LỖI", ex.what());
        show_message_dialog(GTK_WINDOW(state->window), GTK_MESSAGE_ERROR,
                            "Không thể đổi quyền truy cập", ex.what());
    }
}

void on_set_readonly_clicked(GtkButton *, gpointer user_data) {
    perform_access_mode_action(static_cast<GuiState *>(user_data), true);
}

void on_set_readwrite_clicked(GtkButton *, gpointer user_data) {
    perform_access_mode_action(static_cast<GuiState *>(user_data), false);
}

void on_add_whitelist_clicked(GtkButton *, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    const ActiveDevice *device = get_selected_active_device(state);
    const auto rule = prompt_whitelist_rule_dialog(
        GTK_WINDOW(state->window), "Thêm mục vào whitelist",
        "Nhập rule whitelist theo định dạng VVVV:PPPP hoặc VVVV:PPPP:SERIAL.\n"
        "Nếu đang chọn một USB trong danh sách, hệ thống sẽ gợi ý sẵn rule tương ứng.",
        whitelist_rule_for_device(device));

    if (!rule.has_value()) {
        return;
    }

    try {
        if (geteuid() != 0) {
            throw std::runtime_error(
                "Cần chạy giao diện bằng quyền root để thay đổi whitelist runtime.");
        }

        write_control_command_or_throw(control_file_from_status(state->options.status_file),
                                       "add " + *rule);
        append_log(state, "CHÍNH SÁCH",
                   "Đã gửi lệnh thêm whitelist: " + *rule + '.');
        refresh_status_view(state, false);
        persist_whitelist_from_status(state);
        save_gui_config_or_throw(state);
        show_message_dialog(GTK_WINDOW(state->window), GTK_MESSAGE_INFO,
                            "Thêm whitelist thành công",
                            "Whitelist đã được cập nhật trong kernel module và lưu bền vững.");
    } catch (const std::exception &ex) {
        append_log(state, "LỖI", ex.what());
        show_message_dialog(GTK_WINDOW(state->window), GTK_MESSAGE_ERROR,
                            "Không thể thêm whitelist", ex.what());
    }
}

void on_remove_whitelist_clicked(GtkButton *, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);
    const ActiveDevice *device = get_selected_active_device(state);
    const auto rules = split_whitelist_entries(state->current_status.whitelist_entries);

    if (rules.empty()) {
        show_message_dialog(GTK_WINDOW(state->window), GTK_MESSAGE_INFO,
                            "Whitelist đang trống",
                            "Hiện không có mục whitelist nào để xóa.");
        return;
    }

    const auto rule = prompt_select_whitelist_rule_dialog(
        GTK_WINDOW(state->window), rules, whitelist_rule_for_device(device));

    if (!rule.has_value()) {
        return;
    }

    try {
        if (geteuid() != 0) {
            throw std::runtime_error(
                "Cần chạy giao diện bằng quyền root để thay đổi whitelist runtime.");
        }

        write_control_command_or_throw(control_file_from_status(state->options.status_file),
                                       "remove " + *rule);
        append_log(state, "CHÍNH SÁCH",
                   "Đã gửi lệnh xóa whitelist: " + *rule + '.');
        refresh_status_view(state, false);
        persist_whitelist_from_status(state);
        save_gui_config_or_throw(state);
        show_message_dialog(GTK_WINDOW(state->window), GTK_MESSAGE_INFO,
                            "Xóa whitelist thành công",
                            "Kernel module đã cập nhật lại trạng thái whitelist và lưu bền vững.");
    } catch (const std::exception &ex) {
        append_log(state, "LỖI", ex.what());
        show_message_dialog(GTK_WINDOW(state->window), GTK_MESSAGE_ERROR,
                            "Không thể xóa whitelist", ex.what());
    }
}

void apply_css() {
    const char *css =
        "window { background: #eef3f8; color: #16324f; }\n"
        "headerbar { background-image: linear-gradient(135deg, #17324a, #284b63); color: #f6fbff; }\n"
        "headerbar title, headerbar subtitle, headerbar label { color: #f6fbff; }\n"
        "#banner-label {"
        "  background-image: linear-gradient(135deg, #ffffff, #f8fbff);"
        "  color: #17324a;"
        "  border: 1px solid #dce7f1;"
        "  border-left: 6px solid #3f87c5;"
        "  border-radius: 16px;"
        "  padding: 18px;"
        "}\n"
        "#metric-title { color: #5a6f86; font-weight: 700; }\n"
        "#metric-value { color: #12314d; font-size: 19px; font-weight: 800; }\n"
        "#status-label {"
        "  color: #3f4f5f;"
        "  background: #ffffff;"
        "  border: 1px solid #dce7f1;"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}\n"
        "#info-note { color: #5c6f82; font-weight: 600; }\n"
        "frame.metric-card, frame.panel-card {"
        "  background: #ffffff;"
        "  border: 1px solid #d8e3ee;"
        "  border-radius: 16px;"
        "}\n"
        "frame.metric-card > border, frame.panel-card > border { border: none; }\n"
        "frame.panel-card > label {"
        "  color: #4d6277;"
        "  font-weight: 700;"
        "  padding: 6px 10px;"
        "}\n"
        "#toolbar-card {"
        "  background: #ffffff;"
        "  border: 1px solid #d8e3ee;"
        "  border-radius: 16px;"
        "  padding: 12px 14px;"
        "}\n"
        "button {"
        "  background-image: none;"
        "  background: #edf3f8;"
        "  color: #17324a;"
        "  border: 1px solid #c8d8e8;"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "  box-shadow: none;"
        "}\n"
        "button label { color: #17324a; font-weight: 700; }\n"
        "button:hover { background: #dfeaf4; }\n"
        "button.primary-action {"
        "  background: #17324a;"
        "  color: #f6fbff;"
        "  border-color: #17324a;"
        "}\n"
        "button.primary-action:hover { background: #244764; }\n"
        "button.primary-action label { color: #f6fbff; }\n"
        "button.accent-action {"
        "  background: #e5f1ff;"
        "  color: #0c4f88;"
        "  border-color: #b9d4ee;"
        "}\n"
        "button.accent-action:hover { background: #d7e9fc; }\n"
        "button.accent-action label { color: #0c4f88; }\n"
        "button.warning-action {"
        "  background: #fff2f0;"
        "  color: #a84032;"
        "  border-color: #e7b9b3;"
        "}\n"
        "button.warning-action label { color: #a84032; }\n"
        "checkbutton, label { color: #17324a; }\n"
        "checkbutton label { color: #17324a; }\n"
        "spinbutton {"
        "  background: #f8fbff;"
        "  color: #17324a;"
        "  border: 1px solid #c8d8e8;"
        "  border-radius: 10px;"
        "}\n"
        "notebook > header { background: transparent; }\n"
        "notebook > header tabs { margin-bottom: 8px; }\n"
        "notebook > header tabs tab {"
        "  background: #dfe8f0;"
        "  color: #4d6277;"
        "  border-radius: 12px 12px 0 0;"
        "  padding: 8px 14px;"
        "}\n"
        "notebook > header tabs tab label { color: #4d6277; font-weight: 700; }\n"
        "notebook > header tabs tab:checked {"
        "  background: #17324a;"
        "  color: #f6fbff;"
        "}\n"
        "notebook > header tabs tab:checked label { color: #f6fbff; }\n"
        "notebook stack {"
        "  background: #ffffff;"
        "  border: 1px solid #d8e3ee;"
        "  border-radius: 18px;"
        "}\n"
        "treeview.view {"
        "  background: #fbfdff;"
        "  color: #17324a;"
        "  border-radius: 12px;"
        "}\n"
        "treeview.view:selected, treeview.view:selected:focus {"
        "  background: #dceeff;"
        "  color: #0f2940;"
        "}\n"
        "treeview.view header button {"
        "  background: #edf3f8;"
        "  color: #4d6277;"
        "  border: none;"
        "  border-bottom: 1px solid #d8e3ee;"
        "  font-weight: 700;"
        "  padding: 8px 10px;"
        "}\n"
        "textview, textview text {"
        "  background: #fbfdff;"
        "  color: #17324a;"
        "}\n"
        "scrolledwindow {"
        "  border-radius: 14px;"
        "}\n"
        ".mono { font-family: Monospace; }\n";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GtkWidget *create_metric_card(const char *title, GtkWidget **value_out) {
    GtkWidget *frame = gtk_frame_new(nullptr);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *value_label = gtk_label_new("...");

    gtk_widget_set_hexpand(frame, TRUE);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(value_label), 0.0f);
    gtk_widget_set_name(title_label, "metric-title");
    gtk_widget_set_name(value_label, "metric-value");
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "metric-card");

    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), value_label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), box);

    *value_out = value_label;
    return frame;
}

GtkWidget *create_summary_grid(GuiState *state) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    GtkWidget *module_card = create_metric_card("Module", &state->module_value);
    GtkWidget *mode_card = create_metric_card("Chế độ", &state->mode_value);
    GtkWidget *event_card = create_metric_card("Sự kiện cuối", &state->event_value);
    GtkWidget *active_card = create_metric_card("USB đang kết nối", &state->active_value);
    GtkWidget *history_card = create_metric_card("USB trong lịch sử", &state->history_value);
    GtkWidget *trusted_card = create_metric_card("USB tin cậy", &state->trusted_value);
    GtkWidget *untrusted_card =
        create_metric_card("USB ngoài whitelist", &state->untrusted_value);
    GtkWidget *whitelist_card =
        create_metric_card("Danh sách whitelist", &state->whitelist_value);

    gtk_grid_attach(GTK_GRID(grid), module_card, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), mode_card, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), event_card, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), active_card, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), history_card, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), trusted_card, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), untrusted_card, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), whitelist_card, 3, 1, 1, 1);

    return grid;
}

GtkWidget *create_active_page(GuiState *state) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *frame = gtk_frame_new("Thiết bị USB đang kết nối");
    GtkWidget *details_frame = gtk_frame_new("Chi tiết thiết bị được chọn");
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget *details_scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget *detail_view = gtk_text_view_new();

    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_frame_set_shadow_type(GTK_FRAME(details_frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "panel-card");
    gtk_style_context_add_class(gtk_widget_get_style_context(details_frame), "panel-card");

    state->active_store = gtk_list_store_new(
        ACTIVE_COL_COUNT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    state->active_tree =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(state->active_store));

    append_text_column(state->active_tree, "Thiết bị", ACTIVE_COL_DEVICE, -1, TRUE);
    append_text_column(state->active_tree, "Trạng thái", ACTIVE_COL_TRUST,
                       ACTIVE_COL_TRUST_COLOR);
    append_text_column(state->active_tree, "Truy cập", ACTIVE_COL_ACCESS);
    append_text_column(state->active_tree, "Sản phẩm", ACTIVE_COL_PRODUCT, -1, TRUE);
    append_text_column(state->active_tree, "Tốc độ I/O", ACTIVE_COL_IO_RATE);
    append_text_column(state->active_tree, "Lưu lượng", ACTIVE_COL_IO_TOTAL);
    append_text_column(state->active_tree, "Mountpoint", ACTIVE_COL_MOUNTS, -1, TRUE);

    gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(state->active_tree), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(state->active_tree), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(state->active_tree),
                                 GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(state->active_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(on_active_selection_changed), state);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), state->active_tree);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);

    state->detail_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(detail_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(detail_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(detail_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(detail_view), GTK_WRAP_WORD_CHAR);
    gtk_style_context_add_class(gtk_widget_get_style_context(detail_view), "mono");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(details_scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(details_scrolled), detail_view);
    gtk_container_add(GTK_CONTAINER(details_frame), details_scrolled);

    gtk_paned_pack1(GTK_PANED(paned), frame, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), details_frame, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);
    gtk_paned_set_position(GTK_PANED(paned), 860);

    return root;
}

GtkWidget *create_history_page(GuiState *state) {
    GtkWidget *frame = gtk_frame_new("Lịch sử thiết bị USB");
    GtkWidget *scrolled = gtk_scrolled_window_new(nullptr, nullptr);

    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "panel-card");

    state->history_store = gtk_list_store_new(
        HISTORY_COL_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING);
    state->history_tree =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(state->history_store));

    append_text_column(state->history_tree, "VID:PID", HISTORY_COL_VIDPID);
    append_text_column(state->history_tree, "Serial", HISTORY_COL_SERIAL, -1, TRUE);
    append_text_column(state->history_tree, "Số lần cắm", HISTORY_COL_CONNECT_COUNT);
    append_text_column(state->history_tree, "Đang hoạt động", HISTORY_COL_ACTIVE_INSTANCES);
    append_text_column(state->history_tree, "Tổng thời gian", HISTORY_COL_TOTAL_DURATION);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(state->history_tree),
                                 GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), state->history_tree);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);
    return frame;
}

GtkWidget *create_log_page(GuiState *state) {
    GtkWidget *frame = gtk_frame_new("Nhật ký sự kiện và chính sách");
    GtkWidget *scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    GtkWidget *text_view = gtk_text_view_new();

    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "panel-card");

    state->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_style_context_add_class(gtk_widget_get_style_context(text_view), "mono");

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), text_view);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);

    return frame;
}

GtkWidget *create_toolbar(GuiState *state) {
    GtkWidget *frame = gtk_frame_new(nullptr);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *refresh_button = gtk_button_new_with_label("Làm mới ngay");
    GtkWidget *readonly_button =
        gtk_button_new_with_label("Đặt chỉ đọc cho USB chọn");
    GtkWidget *readwrite_button =
        gtk_button_new_with_label("Cho phép đọc/ghi cho USB chọn");
    GtkWidget *add_whitelist_button =
        gtk_button_new_with_label("Thêm whitelist");
    GtkWidget *remove_whitelist_button =
        gtk_button_new_with_label("Xóa whitelist");
    GtkWidget *clear_log_button = gtk_button_new_with_label("Xóa nhật ký");
    GtkWidget *auto_refresh = gtk_check_button_new_with_label("Tự động làm mới");
    GtkWidget *interval_label = gtk_label_new("Chu kỳ (giây)");
    GtkWidget *interval_spin = gtk_spin_button_new_with_range(0.5, 60.0, 0.5);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_refresh), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(interval_spin), 1.0);
    gtk_label_set_xalign(GTK_LABEL(interval_label), 0.0f);
    gtk_widget_set_hexpand(spacer, TRUE);

    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_widget_set_name(frame, "toolbar-card");
    gtk_container_set_border_width(GTK_CONTAINER(box), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(readonly_button),
                                "warning-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(readwrite_button),
                                "accent-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_whitelist_button),
                                "accent-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(remove_whitelist_button),
                                "warning-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(clear_log_button),
                                "warning-action");

    state->auto_refresh_check = auto_refresh;
    state->interval_spin = interval_spin;
    state->readonly_button = readonly_button;
    state->readwrite_button = readwrite_button;

    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), state);
    g_signal_connect(readonly_button, "clicked",
                     G_CALLBACK(on_set_readonly_clicked), state);
    g_signal_connect(readwrite_button, "clicked",
                     G_CALLBACK(on_set_readwrite_clicked), state);
    g_signal_connect(add_whitelist_button, "clicked",
                     G_CALLBACK(on_add_whitelist_clicked), state);
    g_signal_connect(remove_whitelist_button, "clicked",
                     G_CALLBACK(on_remove_whitelist_clicked), state);
    g_signal_connect(clear_log_button, "clicked",
                     G_CALLBACK(on_clear_log_clicked), state);
    g_signal_connect(auto_refresh, "toggled",
                     G_CALLBACK(on_auto_refresh_toggled), state);
    g_signal_connect(interval_spin, "value-changed",
                     G_CALLBACK(on_interval_changed), state);

    gtk_box_pack_start(GTK_BOX(left_box), refresh_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_box), auto_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_box), interval_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_box), interval_spin, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(right_box), readonly_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_box), readwrite_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_box), add_whitelist_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_box), remove_whitelist_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_box), clear_log_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), left_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), spacer, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), right_box, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), box);
    update_access_action_buttons(state);

    return frame;
}

void build_gui(GtkApplication *application, GuiState *state) {
    apply_css();

    state->application = application;
    state->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(state->window),
                         "USB Guard - Bảng điều khiển giám sát USB");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1450, 900);
    gtk_window_set_position(GTK_WINDOW(state->window), GTK_WIN_POS_CENTER);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header),
                             "USB Guard - Giám sát USB trên Ubuntu 64-bit");
    gtk_window_set_titlebar(GTK_WINDOW(state->window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(root), 12);
    gtk_container_add(GTK_CONTAINER(state->window), root);

    state->banner_label = gtk_label_new(nullptr);
    gtk_widget_set_name(state->banner_label, "banner-label");
    gtk_label_set_line_wrap(GTK_LABEL(state->banner_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(state->banner_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(root), state->banner_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), create_toolbar(state), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), create_summary_grid(state), FALSE, FALSE, 0);

    GtkWidget *note_label = gtk_label_new(
        "Ứng dụng GUI đọc dữ liệu từ /proc/usb_guard/status và sysfs. "
        "GUI cho phép quản lý whitelist và quyền truy cập theo từng USB đang chọn, còn cơ chế chặn USB lạ được thực hiện trong kernel module.");
    gtk_widget_set_name(note_label, "info-note");
    gtk_label_set_line_wrap(GTK_LABEL(note_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(root), note_label, FALSE, FALSE, 0);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_box_pack_start(GTK_BOX(root), notebook, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), create_active_page(state),
                             gtk_label_new("Thiết bị đang kết nối"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), create_history_page(state),
                             gtk_label_new("Lịch sử"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), create_log_page(state),
                             gtk_label_new("Nhật ký"));

    state->status_label = gtk_label_new("");
    gtk_widget_set_name(state->status_label, "status-label");
    gtk_label_set_xalign(GTK_LABEL(state->status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(root), state->status_label, FALSE, FALSE, 0);

    gtk_widget_show_all(state->window);
}

void on_activate(GtkApplication *application, gpointer user_data) {
    auto *state = static_cast<GuiState *>(user_data);

    if (state->window != nullptr) {
        gtk_window_present(GTK_WINDOW(state->window));
        return;
    }

    build_gui(application, state);
    refresh_status_view(state, false);
    restart_refresh_timer(state);
}

void print_usage(const char *progname) {
    std::cout
        << "Cách dùng: " << progname << " [tùy chọn]\n"
        << "  --status-file <đường_dẫn>   Chỉ định tệp trạng thái, mặc định /proc/usb_guard/status\n"
        << "  --config-file <đường_dẫn>   Chỉ định tệp cấu hình bền vững, mặc định /etc/usb_guard.conf\n"
        << "  --help                      Hiển thị trợ giúp\n";
}

GuiOptions parse_args(int argc, char **argv) {
    GuiOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--status-file") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Thiếu giá trị cho --status-file");
            }
            options.status_file = argv[++i];
        } else if (arg == "--config-file") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Thiếu giá trị cho --config-file");
            }
            options.config_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("Tùy chọn không hợp lệ: " + arg);
        }
    }

    return options;
}

}  // namespace

int main(int argc, char **argv) {
    try {
        GuiState state;
        state.options = parse_args(argc, argv);
        char *app_argv[] = {argv[0], nullptr};
        int app_argc = 1;

        GtkApplication *application =
            gtk_application_new("com.openai.usbguardgui",
                                G_APPLICATION_DEFAULT_FLAGS);
        g_signal_connect(application, "activate", G_CALLBACK(on_activate), &state);
        const int status =
            g_application_run(G_APPLICATION(application), app_argc, app_argv);
        g_object_unref(application);
        return status;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
