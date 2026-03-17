#include "ast/symbol_pool.h"

namespace stc {

SymbolId SymbolPool::get_id(std::string_view symbol_value) {
    auto it = pool.find(symbol_value);

    if (it != pool.end())
        return it->second;

    if (symbol_value.size() > std::numeric_limits<SymbolId::id_type>::max())
        throw std::runtime_error{
            "Trying to allocate a string that does not fit into SymbolId's size type"};

    // alignment of 1, since alignof(char) is always 1
    auto [offset, mem] =
        arena.allocate(static_cast<SymbolId::id_type>(symbol_value.size()) + 1U, 1);
    char* data_ptr = static_cast<char*>(mem);

    std::copy(symbol_value.begin(), symbol_value.end(), data_ptr);
    data_ptr[symbol_value.size()] = '\0';

    SymbolId id{offset};
    std::string_view pool_view{data_ptr, symbol_value.size()};
    pool.emplace(pool_view, id);

    return id;
}

std::string_view SymbolPool::get_symbol(SymbolId id) const {
    assert(!id.is_null() && "lookup of null symbol id");
    assert(arena.get_ptr<char>(id) != nullptr && "lookup of symbol id not present in pool");

    char* data_ptr = arena.get_ptr<char>(id);

    return std::string_view{data_ptr};
}

bool SymbolPool::has_id(SymbolId id) const {
    return arena.get_ptr(id) != nullptr;
}

std::optional<std::string_view> SymbolPool::get_symbol_maybe(SymbolId id) const {
    if (id.is_null())
        return std::nullopt;

    auto* result = arena.get_ptr<char>(id);

    if (result == nullptr)
        return std::nullopt;

    return std::string_view{result};
}

} // namespace stc
