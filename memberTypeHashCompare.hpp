#pragma once

template <auto memberPtr>
struct Hasher;
template <auto memberPtr>
struct Compare;

// no simple way to describe the deduction here, no way to describe the member
// ptr without member type Unless std::hash<Key> is a program-defined
// specialization, h(k1) will never throw an exception.  - TODO: detect
// otherwise
template <class ClassType, class MemberType, MemberType ClassType::*memberPtr>
struct Hasher<memberPtr> {
    using is_transparent = void;
    [[no_unique_address]] std::hash<MemberType> HashFn{};
    size_t operator()(const ClassType& value) const noexcept {
        return HashFn((&value)->*memberPtr);
    }
    size_t operator()(const MemberType& value) const noexcept {
        return HashFn(value);
    }
};
template <class ClassType, class MemberType, MemberType ClassType::*memberPtr>
struct Compare<memberPtr> {
    using is_transparent = void;
    bool operator()(const ClassType& lhs, const ClassType& rhs) const noexcept {
        return ((&lhs)->*memberPtr) == ((&rhs)->*memberPtr);
    }
    bool operator()(const MemberType& lhs,
                    const MemberType& rhs) const noexcept {
        return lhs == rhs;
    }
    bool operator()(const MemberType& lhs,
                    const ClassType& rhs) const noexcept {
        return lhs == ((&rhs)->*memberPtr);
    }
};
