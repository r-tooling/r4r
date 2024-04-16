#pragma once

struct Graph;

struct Hasher {
	using is_transparent = void;
	size_t operator()(const std::unique_ptr < Graph>& value) const;
	size_t operator()(const std::string& value) const {
		return std::hash<std::string>{}(value);
	}
};
struct Compare {
	using is_transparent = void;
	bool operator()(const std::unique_ptr <Graph>& lhs, const std::unique_ptr <Graph>& rhs) const;
	bool operator()(const std::string& lhs, const std::string& rhs) const {
		return lhs == rhs;
	}
	bool operator()(const std::string& lhs, const std::unique_ptr <Graph>& rhs) const;
};

template<class T>
concept ptr = std::is_same_v<T, std::unique_ptr<Graph>>;
struct Graph {
	enum class operationType {
		none,//nothing not explicitly mentioned in the directory including me shall be of interest
		exact,//only match exectly and the name
		directory//all items in the directory not directly mentioned are special cased.
	};

	using ref = std::unordered_set<std::unique_ptr<Graph>, Hasher, Compare>;

	const std::string dir;
	ref files;
	operationType operation;
	bool operator==(const Graph& other)const noexcept {
		return dir == other.dir;
	}
	bool operator==(const std::string& other)const noexcept {
		return dir == other;
	}
	template<ptr ...T>
	Graph(const std::string& d, operationType f, T... ptrs) :dir(d), files(), operation(f) {
		(files.emplace(std::move(ptrs)), ...);
	}
	Graph(std::string d, operationType f) :dir(d), files(), operation(f) {
	}
	Graph(const Graph&) = delete;
	Graph(Graph&&) = default;
};
size_t Hasher::operator()(const std::unique_ptr < Graph>& value) const
{
	return std::hash<std::string>{}(value->dir);
}
bool Compare::operator()(const std::unique_ptr<Graph>& lhs, const std::unique_ptr<Graph>& rhs) const
{
	return lhs->dir == rhs->dir;
}

bool Compare::operator()(const std::string& lhs, const std::unique_ptr<Graph>& rhs) const
{
	return lhs == rhs->dir;
}