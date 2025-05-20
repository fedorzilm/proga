// unit_tests/test_thread_pool.cpp
#include "gtest/gtest.h"
#include "thread_pool.h" // Предполагается, что common_defs.h и logger.h включены через него
#include "logger.h"

#include <vector>
#include <atomic>
#include <chrono>
#include <thread>     // Для std::this_thread::sleep_for
#include <future>     // Для std::async, std::future, std::promise (для более сложных тестов)
#include <set>        // Для проверки уникальности ID потоков

// Вспомогательная функция для имитации некоторой работы
void simple_task_increment_atomic(std::atomic<int>& counter, int sleep_ms = 10) {
    if (sleep_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    counter++;
}

// Задача, которая выбрасывает исключение
void task_that_throws() {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    throw std::runtime_error("Исключение тестовой задачи");
}

class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Убедимся, что логгер инициализирован, если ThreadPool его использует
        // Для тестов можно направить вывод логов в отдельный файл или установить уровень DEBUG
        // Logger::init(LogLevel::DEBUG, "test_thread_pool.log");
    }

    void TearDown() override {
        // Можно добавить специфичную очистку, если требуется
    }
};

TEST_F(ThreadPoolTest, Constructor_ZeroThreadsThrows) {
    EXPECT_THROW(ThreadPool pool(0), std::invalid_argument);
}

TEST_F(ThreadPoolTest, Constructor_ValidNumberOfThreads) {
    EXPECT_NO_THROW(ThreadPool pool(1));
    EXPECT_NO_THROW(ThreadPool pool(4));
    // Деструктор должен корректно остановить потоки
}

TEST_F(ThreadPoolTest, EnqueueAndExecute_SingleTask) {
    ThreadPool pool(1);
    std::atomic<int> task_execution_count(0);

    EXPECT_TRUE(pool.enqueue([&task_execution_count]() {
        simple_task_increment_atomic(task_execution_count, 5);
    }));

    pool.stop(); // Блокирует до завершения всех задач
    EXPECT_EQ(task_execution_count.load(), 1);
    EXPECT_FALSE(pool.isRunning());
}

TEST_F(ThreadPoolTest, EnqueueAndExecute_MultipleTasksSingleThread) {
    ThreadPool pool(1); // Один рабочий поток
    std::atomic<int> task_execution_count(0);
    const int num_tasks = 5;

    for (int i = 0; i < num_tasks; ++i) {
        EXPECT_TRUE(pool.enqueue([&task_execution_count]() {
            simple_task_increment_atomic(task_execution_count, 5);
        }));
    }
    pool.stop();
    EXPECT_EQ(task_execution_count.load(), num_tasks);
}

TEST_F(ThreadPoolTest, EnqueueAndExecute_MultipleTasksMultipleThreads) {
    const size_t num_threads = 4;
    ThreadPool pool(num_threads);
    std::atomic<int> task_execution_count(0);
    const int num_tasks = 20; // Больше, чем потоков, для проверки распределения

    for (int i = 0; i < num_tasks; ++i) {
        EXPECT_TRUE(pool.enqueue([&task_execution_count]() {
            simple_task_increment_atomic(task_execution_count, (rand() % 10) + 5); // Разное время выполнения
        }));
    }
    pool.stop();
    EXPECT_EQ(task_execution_count.load(), num_tasks);
}


TEST_F(ThreadPoolTest, Stop_EmptyPoolImmediately) {
    ThreadPool pool(2);
    EXPECT_TRUE(pool.isRunning());
    pool.stop();
    EXPECT_FALSE(pool.isRunning());
}

TEST_F(ThreadPoolTest, Stop_WithQueuedTasksNotYetStarted) {
    ThreadPool pool(1); // Один поток, чтобы задачи точно были в очереди
    std::atomic<int> counter(0);
    const int num_tasks = 3;

    // Первая задача, которая будет выполняться долго
    std::promise<void> first_task_started_promise;
    std::future<void> first_task_started_future = first_task_started_promise.get_future();
    std::promise<void> allow_first_task_to_finish_promise;
    std::future<void> allow_first_task_to_finish_future = allow_first_task_to_finish_promise.get_future();

    pool.enqueue([&counter, &first_task_started_promise, &allow_first_task_to_finish_future]() {
        first_task_started_promise.set_value();
        allow_first_task_to_finish_future.wait_for(std::chrono::seconds(1)); // Ждем сигнала
        simple_task_increment_atomic(counter, 0);
    });

    // Добавляем еще задачи, пока первая выполняется
    first_task_started_future.wait(); // Убедимся, что первая задача началась
    for (int i = 0; i < num_tasks - 1; ++i) {
        pool.enqueue([&counter]() { simple_task_increment_atomic(counter, 1); });
    }

    // Теперь останавливаем пул. Ожидаем, что все задачи (включая первую) завершатся.
    allow_first_task_to_finish_promise.set_value(); // Позволяем первой задаче завершиться
    pool.stop();

    EXPECT_EQ(counter.load(), num_tasks);
    EXPECT_FALSE(pool.isRunning());
}

TEST_F(ThreadPoolTest, Enqueue_ReturnsFalse_AfterStopIsCalled) {
    ThreadPool pool(1);
    std::atomic<int> counter(0);

    pool.stop(); // Инициируем остановку
    EXPECT_FALSE(pool.isRunning());

    bool enqueue_result = pool.enqueue([&counter]() { simple_task_increment_atomic(counter, 0); });
    EXPECT_FALSE(enqueue_result);

    // Небольшая пауза, чтобы убедиться, что задача (если бы она была добавлена ошибочно) не выполнилась
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(counter.load(), 0);
}

TEST_F(ThreadPoolTest, Enqueue_NullFunction) {
    ThreadPool pool(1);
    EXPECT_FALSE(pool.enqueue(nullptr)); // Передача пустой std::function
    // Можно добавить задачу после этого, чтобы убедиться, что пул все еще работает
    std::atomic<int> counter(0);
    pool.enqueue([&counter](){ counter++; });
    pool.stop();
    EXPECT_EQ(counter.load(), 1);
}


TEST_F(ThreadPoolTest, TaskThrowsException_PoolContinues) {
    // Logger::init(LogLevel::ERROR); // Предполагается, что логгер перехватит и залогирует ошибку
    ThreadPool pool(1);
    std::atomic<int> counter(0);

    EXPECT_TRUE(pool.enqueue(task_that_throws));
    EXPECT_TRUE(pool.enqueue([&counter]() { simple_task_increment_atomic(counter, 5); }));

    pool.stop();
    // Главное, что вторая задача выполнилась, и пул не упал
    EXPECT_EQ(counter.load(), 1);
}

TEST_F(ThreadPoolTest, IsRunning_CorrectStates) {
    ThreadPool pool(1);
    EXPECT_TRUE(pool.isRunning());
    pool.stop();
    EXPECT_FALSE(pool.isRunning());
}

TEST_F(ThreadPoolTest, MultipleStopCalls_AreSafe) {
    ThreadPool pool(2);
    pool.stop();
    EXPECT_FALSE(pool.isRunning());
    EXPECT_NO_THROW(pool.stop()); // Повторный вызов stop()
    EXPECT_FALSE(pool.isRunning());
}

TEST_F(ThreadPoolTest, WorkerThreadIdsAreUniqueAndCorrectCount) {
    const size_t num_threads = 3;
    ThreadPool pool(num_threads);
    std::set<std::thread::id> thread_ids_from_tasks;
    std::mutex set_mutex; // Для защиты доступа к set
    std::vector<std::promise<void>> task_completion_promises(num_threads * 2);
    std::vector<std::future<void>> task_completion_futures;

    for(size_t i = 0; i < num_threads * 2; ++i) {
        task_completion_futures.push_back(task_completion_promises[i].get_future());
        pool.enqueue([&thread_ids_from_tasks, &set_mutex, &task_completion_promises, i]() {
            {
                std::lock_guard<std::mutex> lock(set_mutex);
                thread_ids_from_tasks.insert(std::this_thread::get_id());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            task_completion_promises[i].set_value();
        });
    }

    // Ожидаем завершения всех задач
    for(auto& fut : task_completion_futures) {
        fut.wait_for(std::chrono::seconds(1)); // Таймаут на случай зависания
    }

    pool.stop();

    // Количество уникальных ID потоков, выполнивших задачи, должно быть равно num_threads,
    // если задач было достаточно, чтобы загрузить все потоки.
    if (num_threads > 0) {
         EXPECT_GE(thread_ids_from_tasks.size(), 1u); // Хотя бы один поток должен был поработать
         EXPECT_LE(thread_ids_from_tasks.size(), num_threads);
    } else {
        EXPECT_EQ(thread_ids_from_tasks.size(), 0u);
    }
}

TEST_F(ThreadPoolTest, DestructorStopsPool) {
    std::atomic<int> counter(0);
    {
        ThreadPool pool(2);
        pool.enqueue([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter++;
        });
        pool.enqueue([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter++;
        });
        // Деструктор будет вызван при выходе из области видимости
    }
    // К моменту выхода из блока, деструктор должен был дождаться завершения задач
    EXPECT_EQ(counter.load(), 2);
}
