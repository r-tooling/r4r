#pragma once
#include <optional>
template <class T, class Callback>
std::optional<T> optOR(std::optional<T>&& value, Callback&& callback) {
    if (value.has_value()) {
        return value;
    } else {
        return callback();
    }
};

template <class T, class Callback,
          class ResultType = std::invoke_result_t<Callback, T>>
std::optional<ResultType> optTransform(std::optional<T>&& value,
                                       Callback&& callback) {
    if (value.has_value()) {
        return callback(value.value());
    } else {
        return std::nullopt;
    }
};