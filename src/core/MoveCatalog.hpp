#pragma once
#include "DamageEngine.hpp"
#include <istream>
#include <string>
#include <vector>

namespace whm {
class MoveCatalog {
public:
    // Pipe separated: character|kind|id|level|name|fullhit(0/1)|damage:rate:special:limit;...
    bool read(std::istream &in, std::vector<std::string> *errors = nullptr);
    const Move *find(int character, MoveKind kind, int id, int level) const;
    std::vector<const Move *> forCharacter(int character, MoveKind kind) const;
    size_t size() const { return moves_.size(); }
private:
    std::vector<Move> moves_;
};
} // namespace whm
