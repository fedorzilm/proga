/*!
 * \file thread_pool.h
 * \brief Определяет класс ThreadPool для управления пулом рабочих потоков.
 *
 * ThreadPool позволяет асинхронно выполнять задачи в ограниченном количестве потоков,
 * что помогает управлять ресурсами и избегать чрезмерного создания потоков.
 * Задачи добавляются в очередь и выполняются доступными рабочими потоками.
 */
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "common_defs.h" // Включает <vector>, <thread>, <mutex>, <condition_variable>, <functional>, <queue>, <atomic>
#include "logger.h"      // Для логирования операций пула

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>
#include <atomic>
#include <stdexcept> // Для std::invalid_argument, std::runtime_error

/*!
 * \class ThreadPool
 * \brief Простой пул потоков для асинхронного выполнения задач.
 *
 * Позволяет добавлять задачи типа `std::function<void()>` в очередь,
 * которые затем извлекаются и выполняются рабочими потоками.
 * Пул можно корректно останавливать, дожидаясь завершения всех задач.
 */
class ThreadPool final {
public:
    /*!
     * \brief Конструктор пула потоков.
     * Создает и запускает указанное количество рабочих потоков.
     * \param num_threads Количество рабочих потоков в пуле. Должно быть больше 0.
     * \throw std::invalid_argument если `num_threads` равен 0.
     * \throw std::runtime_error если не удается создать один из рабочих потоков.
     */
    explicit ThreadPool(size_t num_threads);

    /*!
     * \brief Деструктор.
     * Гарантирует корректную остановку пула и завершение всех рабочих потоков.
     * Вызывает `stop()`, если он не был вызван ранее.
     */
    ~ThreadPool();

    // Запрещаем копирование и присваивание, так как управление потоками уникально
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /*!
     * \brief Добавляет новую задачу в очередь на выполнение.
     * Если пул остановлен или находится в процессе остановки, задача не будет добавлена.
     * \param task Функция (или лямбда), представляющая задачу для выполнения.
     * \return `true`, если задача успешно добавлена в очередь.
     * \return `false`, если пул остановлен или передан пустой `task`.
     */
    bool enqueue(std::function<void()> task);

    /*!
     * \brief Инициирует остановку пула потоков.
     * После вызова этого метода новые задачи не будут приниматься.
     * Метод ожидает завершения всех текущих задач и рабочих потоков.
     * Безопасен для многократного вызова.
     */
    void stop();

    /*!
     * \brief Проверяет, активен ли пул потоков.
     * \return `true`, если пул работает и не был остановлен.
     * \return `false`, если пул остановлен или находится в процессе остановки.
     */
    bool isRunning() const noexcept { return running_.load(); }

private:
    std::vector<std::thread> workers_{};            /*!< Вектор рабочих потоков. */
    std::queue<std::function<void()>> tasks_{};     /*!< Очередь задач на выполнение. */

    std::mutex queue_mutex_{};                      /*!< Мьютекс для защиты доступа к очереди задач. */
    std::condition_variable condition_{};           /*!< Условная переменная для синхронизации потоков (ожидание задач). */
    std::atomic<bool> running_{true};               /*!< Флаг, указывающий, активен ли пул (может быть установлен в false при ошибке инициализации или при штатной остановке). */
    std::atomic<bool> stop_initiated_{false};       /*!< Флаг, указывающий, что была инициирована процедура остановки пула. */

    /*!
     * \brief Основная функция рабочего потока.
     * Извлекает задачи из очереди и выполняет их, пока пул активен.
     */
    void worker_thread_loop();
};

#endif // THREAD_POOL_H
