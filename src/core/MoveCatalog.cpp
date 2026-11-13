#include "MoveCatalog.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <sstream>

namespace whm {
namespace {
std::vector<std::string> split(const std::string &line, char delimiter) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string token;
    while (std::getline(in, token, delimiter)) out.push_back(token);
    return out;
}
bool parseInt(const std::string &s, int &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (*end || v < -100000 || v > 100000) return false;
    out = static_cast<int>(v);
    return true;
}
bool parseDouble(const std::string &s, double &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return !*end && std::isfinite(out) && out > 0.0 && out <= 100.0;
}
}

bool MoveCatalog::read(std::istream &stream, std::vector<std::string> *errors) {
    moves_.clear();
    std::string line;
    int lineNumber = 0;
    bool success = true;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto cells = split(line, '|');
        Move move;
        int flag = 0;
        if (cells.size() != 7 || !parseInt(cells[0], move.character) ||
            !parseInt(cells[2], move.id) || !parseInt(cells[3], move.level) ||
            !parseInt(cells[5], flag) || (flag != 0 && flag != 1) ||
            move.character < 0 || move.character > 19 || move.id < 0 || move.level < -1 || move.level > 4) {
            success = false;
            if (errors) errors->push_back("invalid header at line " + std::to_string(lineNumber));
            continue;
        }
        if (cells[1] == "normal") move.kind = MoveKind::Normal;
        else if (cells[1] == "skill") move.kind = MoveKind::Skill;
        else if (cells[1] == "spell") move.kind = MoveKind::Spell;
        else {
            success = false;
            if (errors) errors->push_back("invalid move kind at line " + std::to_string(lineNumber));
            continue;
        }
        move.name = cells[4];
        move.verifiedFullHit = flag == 1;
        bool good = !move.name.empty();
        for (const auto &segment : split(cells[6], ';')) {
            if (segment.empty()) continue;
            const auto fields = split(segment, ':');
            Hit hit;
            if (fields.size() != 4 || !parseInt(fields[0], hit.baseDamage) ||
                !parseDouble(fields[1], hit.rateAfter) ||
                !parseDouble(fields[2], hit.special) || !parseInt(fields[3], hit.limit) ||
                hit.baseDamage < 0 || hit.limit < 0 || hit.limit > 100 || hit.rateAfter > 1.0)
                good = false;
            if (good) move.hits.push_back(hit);
        }
        if (!good || move.hits.empty()) {
            success = false;
            if (errors) errors->push_back("invalid hit list at line " + std::to_string(lineNumber));
            continue;
        }
        const auto duplicate = std::find_if(moves_.begin(), moves_.end(), [&move](const Move &item) {
            return item.character == move.character && item.kind == move.kind &&
                   item.id == move.id && item.level == move.level;
        });
        if (duplicate != moves_.end()) {
            success = false;
            if (errors) errors->push_back("duplicate move at line " + std::to_string(lineNumber));
            continue;
        }
        moves_.push_back(std::move(move));
    }
    return success;
}

const Move *MoveCatalog::find(int character, MoveKind kind, int id, int level) const {
    const Move *fallback = nullptr;
    for (const auto &move : moves_) {
        if (move.character != character || move.kind != kind || move.id != id) continue;
        if (move.level == level) return &move;
        if (move.level == -1) fallback = &move;
    }
    return fallback;
}

std::vector<const Move *> MoveCatalog::forCharacter(int character, MoveKind kind) const {
    std::vector<const Move *> results;
    for (const auto &move : moves_)
        if (move.character == character && move.kind == kind)
            results.push_back(&move);
    return results;
}
} // namespace whm
