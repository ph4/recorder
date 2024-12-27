// Created by Adam Kecskes
// https://github.com/K-Adam/SafeQueue

module;
#include <mutex>
#include <condition_variable>

#include <queue>
#include <utility>

#include <optional>

export module ThreadSafeQueue;

export template<class T>
class ThreadSafeQueue {

	std::queue<T> queue_;

	std::mutex mutex_;
	std::condition_variable condition_var_;

	std::condition_variable sync_wait_;
	bool finish_processing_ = false;
	int sync_counter_ = 0;

	void DecreaseSyncCounter() {
		if (--sync_counter_ == 0) {
			sync_wait_.notify_one();
		}
	}

public:
	typedef typename std::queue<T>::size_type size_type;

	ThreadSafeQueue() = default;

	~ThreadSafeQueue() {
		Finish();
	}

	void Produce(T&& item) {

		std::lock_guard lock(mutex_);

		queue_.push(std::move(item));
		condition_var_.notify_one();

	}

	void Produce(const T& item) {

		std::lock_guard lock(mutex_);

		queue_.push(item);
		condition_var_.notify_one();

	}

	size_type Size() {

		std::lock_guard lock(mutex_);

		return queue_.size();

	}

	[[nodiscard]]
	std::optional<T> Consume() {

		std::lock_guard lock(mutex_);

		if (queue_.empty()) {
			return std::nullopt;
		}

		auto item = std::move(queue_.front());
		queue_.pop();

		return std::move(item);

	}

	[[nodiscard]]
	std::optional<T> ConsumeSync() {

		std::unique_lock<std::mutex> lock(mutex_);

		sync_counter_++;

		condition_var_.wait(lock, [&] {
			return !queue_.empty() || finish_processing_;
		});

		if (queue_.empty()) {
			DecreaseSyncCounter();
			return std::nullopt;
		}

		auto item = std::move(queue_.front());
		queue_.pop();

		DecreaseSyncCounter();
		return item;

	}

	void Finish() {

		std::unique_lock lock(mutex_);

		finish_processing_ = true;
		condition_var_.notify_all();

		sync_wait_.wait(lock, [&]() {
			return sync_counter_ == 0;
		});

		finish_processing_ = false;

	}

};