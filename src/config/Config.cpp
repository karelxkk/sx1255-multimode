#include "sx1255/config/Config.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sx1255 {

namespace {

// ---------------------------------------------------------------------------
// Minimal YAML-subset parser (nested maps + scalar values).  It intentionally
// supports only what the configuration needs; a full YAML parser would pull in
// a dependency, which the project deliberately avoids.
// ---------------------------------------------------------------------------

struct YamlNode {
    enum class Kind { Map, Scalar };

    Kind kind = Kind::Map;
    std::map<std::string, YamlNode> map;
    std::string scalar;
};

struct Line {
    int indent = 0;
    std::string key;
    std::string value;
    bool hasValue = false;
};

std::string trim(std::string s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

std::string stripInlineComment(std::string s)
{
    // Only treat '#' as a comment when preceded by whitespace, so a value
    // such as a frequency is never mangled.
    const auto pos = s.find(" #");
    if (pos != std::string::npos)
        s.erase(pos);
    return trim(s);
}

std::vector<Line> tokenize(std::istream& input)
{
    std::vector<Line> lines;
    std::string raw;
    std::size_t lineNumber = 0;

    while (std::getline(input, raw)) {
        ++lineNumber;
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();

        const auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos || raw[first] == '#')
            continue;

        const auto colon = raw.find(':', first);
        if (colon == std::string::npos)
            throw std::runtime_error("line " + std::to_string(lineNumber) + ": missing ':'");

        Line line;
        line.indent = static_cast<int>(first);
        line.key = trim(raw.substr(first, colon - first));
        line.value = stripInlineComment(raw.substr(colon + 1));
        line.hasValue = !line.value.empty();
        lines.push_back(std::move(line));
    }

    return lines;
}

YamlNode buildNode(const std::vector<Line>& lines, std::size_t& index, int indent)
{
    YamlNode node;
    node.kind = YamlNode::Kind::Map;

    while (index < lines.size()) {
        const Line& line = lines[index];

        if (line.indent < indent)
            break;
        if (line.indent > indent)
            throw std::runtime_error("unexpected indentation for key '" + line.key + "'");

        if (line.hasValue) {
            YamlNode scalar;
            scalar.kind = YamlNode::Kind::Scalar;
            scalar.scalar = line.value;
            node.map[line.key] = std::move(scalar);
            ++index;
            continue;
        }

        ++index;
        if (index < lines.size() && lines[index].indent > indent) {
            node.map[line.key] = buildNode(lines, index, lines[index].indent);
        } else {
            YamlNode empty;
            empty.kind = YamlNode::Kind::Map;
            node.map[line.key] = std::move(empty);
        }
    }

    return node;
}

const YamlNode* child(const YamlNode& node, const std::string& key)
{
    if (node.kind != YamlNode::Kind::Map)
        throw std::runtime_error("'" + key + "' is not a mapping");
    const auto it = node.map.find(key);
    if (it == node.map.end())
        throw std::runtime_error("missing configuration key: '" + key + "'");
    return &it->second;
}

const YamlNode* scalarNode(const YamlNode& node, const std::string& key)
{
    const YamlNode* value = child(node, key);
    if (value->kind != YamlNode::Kind::Scalar)
        throw std::runtime_error("'" + key + "' must be a scalar value");
    return value;
}

long long asInt(const YamlNode& node, const std::string& key)
{
    try {
        return std::stoll(scalarNode(node, key)->scalar);
    } catch (const std::exception&) {
        throw std::runtime_error("'" + key + "' must be an integer");
    }
}

double asDouble(const YamlNode& node, const std::string& key)
{
    try {
        return std::stod(scalarNode(node, key)->scalar);
    } catch (const std::exception&) {
        throw std::runtime_error("'" + key + "' must be a number");
    }
}

bool asBool(const YamlNode& node, const std::string& key)
{
    std::string value = scalarNode(node, key)->scalar;
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (value == "true" || value == "yes" || value == "1")
        return true;
    if (value == "false" || value == "no" || value == "0")
        return false;
    throw std::runtime_error("'" + key + "' must be true or false");
}

bool hasKey(const YamlNode& node, const std::string& key)
{
    return node.kind == YamlNode::Kind::Map && node.map.count(key) != 0;
}

bool optBool(const YamlNode& node, const std::string& key, bool def)
{
    return hasKey(node, key) ? asBool(node, key) : def;
}

long long optInt(const YamlNode& node, const std::string& key, long long def)
{
    return hasKey(node, key) ? asInt(node, key) : def;
}

double optDouble(const YamlNode& node, const std::string& key, double def)
{
    return hasKey(node, key) ? asDouble(node, key) : def;
}

std::string optString(const YamlNode& node, const std::string& key, std::string def)
{
    return hasKey(node, key) ? scalarNode(node, key)->scalar : std::move(def);
}

std::string formatDouble(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

}  // namespace

Config loadConfig(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("cannot open configuration file: " + path);

    const auto lines = tokenize(file);
    if (lines.empty())
        throw std::runtime_error("empty configuration file: " + path);

    std::size_t index = 0;
    const YamlNode root = buildNode(lines, index, lines[0].indent);

    Config config;

    const YamlNode* frontend = child(root, "frontend");
    config.frontend.type        = optString(*frontend, "type", "null");
    config.frontend.rxFrequency = static_cast<Hertz>(asInt(*frontend, "rx_freq"));
    config.frontend.txFrequency = static_cast<Hertz>(asInt(*frontend, "tx_freq"));
    config.frontend.power       = static_cast<dBm>(
        hasKey(*frontend, "tx_power") ? asInt(*frontend, "tx_power") : asInt(*frontend, "power"));
    // Legacy/optional: RSSI-based squelch is not implemented yet, so a
    // missing key must not prevent startup.
    config.frontend.squelch     = static_cast<dBm>(
        optInt(*frontend, "squelch", config.frontend.squelch));
    if (frontend->map.count("block_size"))
        config.frontend.blockSize = static_cast<std::size_t>(asInt(*frontend, "block_size"));

    if (hasKey(*frontend, "spi")) {
        const YamlNode* spi = child(*frontend, "spi");
        config.frontend.spiDevice = optString(*spi, "device", config.frontend.spiDevice);
        config.frontend.spiSpeed = static_cast<std::uint32_t>(optInt(*spi, "speed", config.frontend.spiSpeed));
    }
    if (hasKey(*frontend, "gpio")) {
        const YamlNode* gpio = child(*frontend, "gpio");
        config.frontend.gpioChip     = optString(*gpio, "chip", config.frontend.gpioChip);
        config.frontend.gpioReset    = static_cast<int>(optInt(*gpio, "reset", config.frontend.gpioReset));
        config.frontend.gpioTxEnable = static_cast<int>(optInt(*gpio, "tx_enable", config.frontend.gpioTxEnable));
        config.frontend.gpioRxEnable = static_cast<int>(optInt(*gpio, "rx_enable", config.frontend.gpioRxEnable));
        config.frontend.gpioPtt      = static_cast<int>(optInt(*gpio, "ptt", config.frontend.gpioPtt));
    }
    if (hasKey(*frontend, "alsa")) {
        const YamlNode* alsa = child(*frontend, "alsa");
        config.frontend.alsaCapture  = optString(*alsa, "capture", config.frontend.alsaCapture);
        config.frontend.alsaPlayback = optString(*alsa, "playback", config.frontend.alsaPlayback);
    }
    config.frontend.lnaGain   = static_cast<int>(optInt(*frontend, "lna_gain", config.frontend.lnaGain));
    config.frontend.pgaGain   = static_cast<int>(optInt(*frontend, "pga_gain", config.frontend.pgaGain));
    config.frontend.dacGain   = static_cast<int>(optInt(*frontend, "dac_gain", config.frontend.dacGain));
    config.frontend.mixerGain = optDouble(*frontend, "mixer_gain", config.frontend.mixerGain);

    const YamlNode* modes = child(root, "modes");

    const YamlNode* fm = child(*modes, "fm");
    config.fm.enabled = asBool(*fm, "enabled");
    if (fm->map.count("ctcss_tx"))
        config.fm.ctcssTx = asDouble(*fm, "ctcss_tx");
    if (fm->map.count("rssi_squelch_db"))
        config.fm.rssiSquelchDb = asDouble(*fm, "rssi_squelch_db");
    if (fm->map.count("squelch_hold_ms"))
        config.fm.squelchHoldMs = static_cast<int>(asInt(*fm, "squelch_hold_ms"));
    if (fm->map.count("ctcss")) {
        // Backwards-compatible alias: one tone for both RX and TX.
        const double ctcss = asDouble(*fm, "ctcss");
        if (!fm->map.count("ctcss_tx"))
            config.fm.ctcssTx = ctcss;
    }
    if (fm->map.count("hang_time_ms"))
        config.fm.hangTimeMs = static_cast<int>(asInt(*fm, "hang_time_ms"));
    config.fm.backend = optString(*fm, "backend", config.fm.backend);
    if (hasKey(*fm, "svxlink")) {
        const YamlNode* sx = child(*fm, "svxlink");
        config.fm.svxlink.audioCapture  = optString(*sx, "audio_capture", config.fm.svxlink.audioCapture);
        config.fm.svxlink.audioPlayback = optString(*sx, "audio_playback", config.fm.svxlink.audioPlayback);
        config.fm.svxlink.ptyPath       = optString(*sx, "pty", config.fm.svxlink.ptyPath);
        config.fm.svxlink.sqlPty        = optString(*sx, "sql_pty", config.fm.svxlink.sqlPty);
        config.fm.svxlink.sqlPtyOwner   = optString(*sx, "sql_pty_owner", config.fm.svxlink.sqlPtyOwner);
        config.fm.svxlink.audioRate     = static_cast<int>(optInt(*sx, "audio_rate", config.fm.svxlink.audioRate));
        config.fm.svxlink.rxShiftHz     = optDouble(*sx, "rx_shift_hz", config.fm.svxlink.rxShiftHz);
        config.fm.svxlink.rxMaxDevHz    = optDouble(*sx, "rx_max_dev_hz", config.fm.svxlink.rxMaxDevHz);
        config.fm.svxlink.txDevHz       = optDouble(*sx, "tx_dev_hz", config.fm.svxlink.txDevHz);
        config.fm.svxlink.txGain        = optDouble(*sx, "tx_gain", config.fm.svxlink.txGain);
        config.fm.svxlink.rxDeemphUs    = optDouble(*sx, "rx_deemph_us", config.fm.svxlink.rxDeemphUs);
        config.fm.svxlink.txPreemphUs   = optDouble(*sx, "tx_preemph_us", config.fm.svxlink.txPreemphUs);
        config.fm.svxlink.rxAudioLpfHz  = optDouble(*sx, "rx_audio_lpf_hz", config.fm.svxlink.rxAudioLpfHz);
        config.fm.svxlink.txAudioLpfHz  = optDouble(*sx, "tx_audio_lpf_hz", config.fm.svxlink.txAudioLpfHz);
        config.fm.svxlink.rxHighPassHz  = optDouble(*sx, "rx_highpass_hz", config.fm.svxlink.rxHighPassHz);
    }

    const YamlNode* dstar = child(*modes, "dstar");
    config.dstar.enabled = asBool(*dstar, "enabled");
    config.dstar.callsign = optString(*dstar, "callsign", config.dstar.callsign);
    config.dstar.txPreambleBytes = static_cast<unsigned int>(
        optInt(*dstar, "tx_preamble_bytes", config.dstar.txPreambleBytes));
    config.dstar.txTailMs = static_cast<unsigned int>(
        optInt(*dstar, "tx_tail_ms", config.dstar.txTailMs));
    if (hasKey(*dstar, "gateway")) {
        const YamlNode* gateway = child(*dstar, "gateway");
        config.dstar.gateway.enabled      = optBool(*gateway, "enabled", false);
        config.dstar.gateway.address      = optString(*gateway, "address", config.dstar.gateway.address);
        config.dstar.gateway.port         = static_cast<unsigned int>(
            optInt(*gateway, "port", config.dstar.gateway.port));
        config.dstar.gateway.localAddress = optString(*gateway, "local_address", config.dstar.gateway.localAddress);
        config.dstar.gateway.localPort    = static_cast<unsigned int>(
            optInt(*gateway, "local_port", config.dstar.gateway.localPort));
        config.dstar.gateway.name         = optString(*gateway, "name", config.dstar.gateway.name);
    }

    const YamlNode* m17 = child(*modes, "m17");
    config.m17.enabled = asBool(*m17, "enabled");
    config.m17.callsign = optString(*m17, "callsign", config.m17.callsign);
    config.m17.syncLsfThreshold = optDouble(*m17, "sync_lsf_threshold", config.m17.syncLsfThreshold);
    config.m17.syncPacketThreshold = optDouble(*m17, "sync_packet_threshold", config.m17.syncPacketThreshold);
    config.m17.syncStreamThreshold = optDouble(*m17, "sync_stream_threshold", config.m17.syncStreamThreshold);
    config.m17.syncEotThreshold = optDouble(*m17, "sync_eot_threshold", config.m17.syncEotThreshold);
    config.m17.syncLsfSyncThreshold =
        optDouble(*m17, "sync_lsf_sync_threshold", config.m17.syncLsfSyncThreshold);
    config.m17.syncFollowThreshold = optDouble(*m17, "sync_follow_threshold", config.m17.syncFollowThreshold);
    config.m17.minSyncBurst = static_cast<int>(optInt(*m17, "min_sync_burst", config.m17.minSyncBurst));
    config.m17.idleTimeoutMs = static_cast<int>(optInt(*m17, "idle_timeout_ms", config.m17.idleTimeoutMs));
    config.m17.rssiGateDbfs = optDouble(*m17, "rssi_gate_dbfs", config.m17.rssiGateDbfs);
    config.m17.logSync = optBool(*m17, "log_sync", config.m17.logSync);
    config.m17.networkPacketQueue =
        static_cast<int>(optInt(*m17, "network_packet_queue", config.m17.networkPacketQueue));
    config.m17.networkPacketTtlMs =
        static_cast<int>(optInt(*m17, "network_packet_ttl_ms", config.m17.networkPacketTtlMs));
    if (hasKey(*m17, "bridge")) {
        const YamlNode* bridge = child(*m17, "bridge");
        config.m17.bridge.enabled  = optBool(*bridge, "enabled", false);
        config.m17.bridge.host     = optString(*bridge, "host", config.m17.bridge.host);
        config.m17.bridge.port     = static_cast<int>(optInt(*bridge, "port", config.m17.bridge.port));
        config.m17.bridge.module   = optString(*bridge, "module", config.m17.bridge.module);
        config.m17.bridge.callsign = optString(*bridge, "callsign", config.m17.bridge.callsign);
    }
    if (hasKey(*m17, "reflector")) {
        const YamlNode* reflector = child(*m17, "reflector");
        config.m17.reflector.enabled = optBool(*reflector, "enabled", false);
        config.m17.reflector.host     = optString(*reflector, "host", config.m17.reflector.host);
        config.m17.reflector.port     = static_cast<int>(optInt(*reflector, "port", config.m17.reflector.port));
        config.m17.reflector.module   = optString(*reflector, "module", config.m17.reflector.module);
    }

    if (hasKey(root, "network")) {
        const YamlNode* network = child(root, "network");
        config.network.mode = optString(*network, "mode", config.network.mode);
    }

    if (hasKey(root, "recording")) {
        const YamlNode* recording = child(root, "recording");
        config.recording.enabled = optBool(*recording, "enabled", false);
        config.recording.file    = optString(*recording, "file", config.recording.file);
    }

    if (hasKey(root, "replay")) {
        const YamlNode* replay = child(root, "replay");
        config.replay.enabled = optBool(*replay, "enabled", false);
        config.replay.file    = optString(*replay, "file", config.replay.file);
    }

    if (hasKey(root, "automatic_capture")) {
        const YamlNode* capture = child(root, "automatic_capture");
        config.automaticCapture.enabled   = optBool(*capture, "enabled", false);
        config.automaticCapture.directory = optString(*capture, "directory", config.automaticCapture.directory);
        config.automaticCapture.preSeconds  = optDouble(*capture, "pre_seconds", config.automaticCapture.preSeconds);
        config.automaticCapture.postSeconds = optDouble(*capture, "post_seconds", config.automaticCapture.postSeconds);
        if (hasKey(*capture, "trigger")) {
            const YamlNode* trigger = child(*capture, "trigger");
            config.automaticCapture.triggerFalsePositive = optBool(*trigger, "false_positive", true);
            config.automaticCapture.triggerUnknownSignal = optBool(*trigger, "unknown_signal", true);
            config.automaticCapture.triggerCrash         = optBool(*trigger, "crash", true);
        }
    }

    if (hasKey(root, "spectrum")) {
        const YamlNode* spectrum = child(root, "spectrum");
        config.spectrum.enabled        = optBool(*spectrum, "enabled", true);
        config.spectrum.fftSize        = static_cast<int>(optInt(*spectrum, "fft_size", config.spectrum.fftSize));
        config.spectrum.updateHz       = static_cast<int>(optInt(*spectrum, "update_hz", config.spectrum.updateHz));
        config.spectrum.waterfallRows  = static_cast<int>(optInt(*spectrum, "waterfall_rows", config.spectrum.waterfallRows));
        config.spectrum.waterfallBins  = static_cast<int>(optInt(*spectrum, "waterfall_bins", config.spectrum.waterfallBins));
    }

    return config;
}

Mode parseNetworkMode(const std::string& mode)
{
    if (mode == "fm")    return Mode::FM;
    if (mode == "dstar") return Mode::DStar;
    if (mode == "m17")   return Mode::M17;
    return Mode::Unknown;  // auto or invalid -> follow last local mode
}

std::string serializeConfig(const Config& config)
{
    std::ostringstream out;

    out << "frontend:\n";
    out << "  type: " << config.frontend.type << '\n';
    out << "  rx_freq: " << config.frontend.rxFrequency << '\n';
    out << "  tx_freq: " << config.frontend.txFrequency << '\n';
    out << "  tx_power: " << config.frontend.power << '\n';
    out << "  squelch: " << config.frontend.squelch << '\n';
    out << "  block_size: " << config.frontend.blockSize << '\n';
    out << "  spi:\n";
    out << "    device: " << config.frontend.spiDevice << '\n';
    out << "    speed: " << config.frontend.spiSpeed << '\n';
    out << "  gpio:\n";
    out << "    chip: " << config.frontend.gpioChip << '\n';
    out << "    reset: " << config.frontend.gpioReset << '\n';
    out << "    tx_enable: " << config.frontend.gpioTxEnable << '\n';
    out << "    rx_enable: " << config.frontend.gpioRxEnable << '\n';
    out << "    ptt: " << config.frontend.gpioPtt << '\n';
    out << "  alsa:\n";
    out << "    capture: " << config.frontend.alsaCapture << '\n';
    out << "    playback: " << config.frontend.alsaPlayback << '\n';
    out << "  lna_gain: " << config.frontend.lnaGain << '\n';
    out << "  pga_gain: " << config.frontend.pgaGain << '\n';
    out << "  dac_gain: " << config.frontend.dacGain << '\n';
    out << "  mixer_gain: " << formatDouble(config.frontend.mixerGain) << '\n';
    out << '\n';

    out << "modes:\n";
    out << "  fm:\n";
    out << "    enabled: " << (config.fm.enabled ? "true" : "false") << '\n';
    out << "    ctcss_tx: " << formatDouble(config.fm.ctcssTx) << '\n';
    out << "    rssi_squelch_db: " << formatDouble(config.fm.rssiSquelchDb) << '\n';
    out << "    squelch_hold_ms: " << config.fm.squelchHoldMs << '\n';
    out << "    hang_time_ms: " << config.fm.hangTimeMs << '\n';
    out << "    backend: " << config.fm.backend << '\n';
    out << "    svxlink:\n";
    out << "      audio_capture: " << config.fm.svxlink.audioCapture << '\n';
    out << "      audio_playback: " << config.fm.svxlink.audioPlayback << '\n';
    out << "      pty: " << config.fm.svxlink.ptyPath << '\n';
    out << "      sql_pty: " << config.fm.svxlink.sqlPty << '\n';
    out << "      sql_pty_owner: " << config.fm.svxlink.sqlPtyOwner << '\n';
    out << "      audio_rate: " << config.fm.svxlink.audioRate << '\n';
    out << "      rx_shift_hz: " << formatDouble(config.fm.svxlink.rxShiftHz) << '\n';
    out << "      rx_max_dev_hz: " << formatDouble(config.fm.svxlink.rxMaxDevHz) << '\n';
    out << "      tx_dev_hz: " << formatDouble(config.fm.svxlink.txDevHz) << '\n';
    out << "      tx_gain: " << formatDouble(config.fm.svxlink.txGain) << '\n';
    out << "      rx_deemph_us: " << formatDouble(config.fm.svxlink.rxDeemphUs) << '\n';
    out << "      tx_preemph_us: " << formatDouble(config.fm.svxlink.txPreemphUs) << '\n';
    out << "      rx_audio_lpf_hz: " << formatDouble(config.fm.svxlink.rxAudioLpfHz) << '\n';
    out << "      tx_audio_lpf_hz: " << formatDouble(config.fm.svxlink.txAudioLpfHz) << '\n';
    out << "      rx_highpass_hz: " << formatDouble(config.fm.svxlink.rxHighPassHz) << '\n';
    out << "  dstar:\n";
    out << "    enabled: " << (config.dstar.enabled ? "true" : "false") << '\n';
    out << "    callsign: " << config.dstar.callsign << '\n';
    out << "    tx_preamble_bytes: " << config.dstar.txPreambleBytes << '\n';
    out << "    tx_tail_ms: " << config.dstar.txTailMs << '\n';
    out << "    gateway:\n";
    out << "      enabled: " << (config.dstar.gateway.enabled ? "true" : "false") << '\n';
    out << "      address: " << config.dstar.gateway.address << '\n';
    out << "      port: " << config.dstar.gateway.port << '\n';
    out << "      local_address: " << config.dstar.gateway.localAddress << '\n';
    out << "      local_port: " << config.dstar.gateway.localPort << '\n';
    out << "      name: " << config.dstar.gateway.name << '\n';
    out << "  m17:\n";
    out << "    enabled: " << (config.m17.enabled ? "true" : "false") << '\n';
    out << "    callsign: " << config.m17.callsign << '\n';
    out << "    sync_lsf_threshold: " << config.m17.syncLsfThreshold << '\n';
    out << "    sync_packet_threshold: " << config.m17.syncPacketThreshold << '\n';
    out << "    sync_stream_threshold: " << config.m17.syncStreamThreshold << '\n';
    out << "    sync_eot_threshold: " << config.m17.syncEotThreshold << '\n';
    out << "    sync_lsf_sync_threshold: " << config.m17.syncLsfSyncThreshold << '\n';
    out << "    sync_follow_threshold: " << config.m17.syncFollowThreshold << '\n';
    out << "    min_sync_burst: " << config.m17.minSyncBurst << '\n';
    out << "    idle_timeout_ms: " << config.m17.idleTimeoutMs << '\n';
    out << "    rssi_gate_dbfs: " << config.m17.rssiGateDbfs << '\n';
    out << "    log_sync: " << (config.m17.logSync ? "true" : "false") << '\n';
    out << "    network_packet_queue: " << config.m17.networkPacketQueue << '\n';
    out << "    network_packet_ttl_ms: " << config.m17.networkPacketTtlMs << '\n';
    out << "    bridge:\n";
    out << "      enabled: " << (config.m17.bridge.enabled ? "true" : "false") << '\n';
    out << "      host: " << config.m17.bridge.host << '\n';
    out << "      port: " << config.m17.bridge.port << '\n';
    out << "      module: " << config.m17.bridge.module << '\n';
    out << "      callsign: " << config.m17.bridge.callsign << '\n';
    out << "    reflector:\n";
    out << "      enabled: " << (config.m17.reflector.enabled ? "true" : "false") << '\n';
    out << "      host: " << config.m17.reflector.host << '\n';
    out << "      port: " << config.m17.reflector.port << '\n';
    out << "      module: " << config.m17.reflector.module << '\n';
    out << "network:\n";
    out << "  mode: " << config.network.mode << '\n';
    out << '\n';

    out << "recording:\n";
    out << "  enabled: " << (config.recording.enabled ? "true" : "false") << '\n';
    out << "  file: " << config.recording.file << '\n';
    out << "replay:\n";
    out << "  enabled: " << (config.replay.enabled ? "true" : "false") << '\n';
    out << "  file: " << config.replay.file << '\n';
    out << '\n';

    out << "spectrum:\n";
    out << "  enabled: " << (config.spectrum.enabled ? "true" : "false") << '\n';
    out << "  fft_size: " << config.spectrum.fftSize << '\n';
    out << "  update_hz: " << config.spectrum.updateHz << '\n';
    out << "  waterfall_rows: " << config.spectrum.waterfallRows << '\n';
    out << "  waterfall_bins: " << config.spectrum.waterfallBins << '\n';
    out << '\n';

    out << "automatic_capture:\n";
    out << "  enabled: " << (config.automaticCapture.enabled ? "true" : "false") << '\n';
    out << "  directory: " << config.automaticCapture.directory << '\n';
    out << "  pre_seconds: " << formatDouble(config.automaticCapture.preSeconds) << '\n';
    out << "  post_seconds: " << formatDouble(config.automaticCapture.postSeconds) << '\n';
    out << "  trigger:\n";
    out << "    false_positive: " << (config.automaticCapture.triggerFalsePositive ? "true" : "false") << '\n';
    out << "    unknown_signal: " << (config.automaticCapture.triggerUnknownSignal ? "true" : "false") << '\n';
    out << "    crash: " << (config.automaticCapture.triggerCrash ? "true" : "false") << '\n';

    return out.str();
}

}  // namespace sx1255
