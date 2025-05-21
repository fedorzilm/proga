/*!
 * \file thread_pool.cpp
 * \author Fedor Zilnitskiy
 * \brief Реализация класса ThreadPool для управления пулом рабочих потоков.
 */
#include "thread_pool.h"

/*!
 * \brief Конструктор ThreadPool.
 * \param num_threads Количество рабочих потоков.
 */
ThreadPool::ThreadPool(size_t num_threads) : running_(true), stop_initiated_(false) {
    if (num_threads == 0) {
        Logger::error("Конструктор ThreadPool: Количество потоков не может быть равно 0.");
        throw std::invalid_argument("ThreadPool: Количество потоков не может быть равно 0.");
    }
    Logger::info("Конструктор ThreadPool: Создание пула с " + std::to_string(num_threads) + " потоками.");
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        try {
            workers_.emplace_back(&ThreadPool::worker_thread_loop, this);
        } catch (const std::system_error& e) {
            Logger::error("Конструктор ThreadPool: Не удалось создать рабочий поток #" + std::to_string(i) + ". Ошибка: " + e.what());
            // Попытка остановить уже созданные потоки, если не все удалось запустить
            stop_initiated_.store(true); 
            running_.store(false);       
            condition_.notify_all();    

            for (size_t j = 0; j < workers_.size(); ++j) { // workers_.size() будет меньше i
                if (workers_[j].joinable()) {
                    try {
                         workers_[j].join();
                    } catch (const std::system_error& join_e) {
                         Logger::error("Конструктор ThreadPool: Ошибка при join потока #" + std::to_string(j) + " во время аварийной остановки: " + join_e.what());
                    }
                }
            }
            workers_.clear(); // Очищаем вектор, так как он не полностью инициализирован
            throw std::runtime_error("Конструктор ThreadPool: Не удалось инициализировать все рабочие потоки. Пул остановлен.");
        }
    }
    Logger::info("Конструктор ThreadPool: Пул потоков успешно запущен. Активных потоков: " + std::to_string(workers_.size()));
}

/*!
 * \brief Деструктор ThreadPool.
 */
ThreadPool::~ThreadPool() {
    Logger::debug("Деструктор ThreadPool: Деструктор вызван.");
    // Убедимся, что stop() был вызван, если пул еще считается активным
    if (running_.load() || !stop_initiated_.load()) { 
        Logger::info("Деструктор ThreadPool: Пул не был явно остановлен, вызов stop().");
        stop();
    } else {
        // Если stop() был вызван, потоки уже должны быть присоединены.
        // На всякий случай, проверим еще раз, если stop() по какой-то причине не завершил все.
        bool all_joined = true;
        for(const auto& worker : workers_){
            if(worker.joinable()){
                all_joined = false;
                break;
            }
        }
        if(!all_joined && workers_.size() > 0){ // workers_.size() > 0 на случай если stop() очистил workers_
             Logger::warn("Деструктор ThreadPool: Обнаружены 'joinable' потоки после вызова stop(). Повторная попытка join (это не должно происходить).");
             for (std::thread& worker : workers_) {
                if (worker.joinable()) {
                    try { worker.join(); } catch (const std::system_error&) {}
                }
            }
        }
    }
    Logger::info("Деструктор ThreadPool: Пул потоков уничтожен.");
}

/*!
 * \brief Основной цикл рабочего потока.
 */
void ThreadPool::worker_thread_loop() {
    const std::string thread_id_str = Logger::get_thread_id_str();
    Logger::debug("Рабочий поток ThreadPool " + thread_id_str + ": Запущен.");
    std::function<void()> task;

    while (true) { // Цикл будет прерван return'ами
        { // Начало критической секции для доступа к очереди
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return stop_initiated_.load() || !tasks_.empty(); });

            if (stop_initiated_.load() && tasks_.empty()) {
                Logger::debug("Рабочий поток ThreadPool " + thread_id_str + ": Остановка (stop_initiated и очередь пуста).");
                return; // Выход из потока
            }

            // Если проснулись, но задач нет (могло быть из-за stop_initiated_ без задач или spurious wakeup)
            if (tasks_.empty()) { 
                if (stop_initiated_.load()) { // Если это из-за остановки, выходим
                     Logger::debug("Рабочий поток ThreadPool " + thread_id_str + ": Остановка (очередь пуста после пробуждения по stop_initiated).");
                     return;
                }
                continue; // Spurious wakeup, продолжаем ожидание
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        } // Конец критической секции, мьютекс освобожден

        if (task) { // Убедимся, что задача валидна (хотя enqueue не должен добавлять пустые)
            Logger::debug("Рабочий поток ThreadPool " + thread_id_str + ": Начинает выполнение задачи.");
            try {
                task();
            } catch (const std::exception& e) {
                Logger::error("Рабочий поток ThreadPool " + thread_id_str + ": Поймано std::exception при выполнении задачи: " + e.what());
            } catch (...) {
                Logger::error("Рабочий поток ThreadPool " + thread_id_str + ": Поймано неизвестное исключение при выполнении задачи.");
            }
            Logger::debug("Рабочий поток ThreadPool " + thread_id_str + ": Завершил выполнение задачи.");
            task = nullptr; // Освобождаем задачу
        }
    }
     // Сюда не должны доходить, выход через return в цикле
}

/*!
 * \brief Добавляет задачу в очередь пула.
 * \param task Задача для выполнения.
 * \return true если задача добавлена, false если пул остановлен.
 */
bool ThreadPool::enqueue(std::function<void()> task) {
    if (stop_initiated_.load()) { // Не принимаем новые задачи, если инициирована остановка
        Logger::warn("ThreadPool Enqueue: Попытка добавить задачу в пул, который останавливается или уже остановлен.");
        return false;
    }
    if (!task) { // Проверка на пустую функцию
        Logger::warn("ThreadPool Enqueue: Попытка добавить пустую задачу (std::function был nullptr).");
        return false; 
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // Дополнительная проверка под мьютексом на случай, если stop_initiated_ установился между первой проверкой и захватом мьютекса
        if (stop_initiated_.load()) { 
            Logger::warn("ThreadPool Enqueue: Пул был остановлен во время добавления задачи в очередь.");
            return false;
        }
        tasks_.push(std::move(task));
    }
    condition_.notify_one(); // Разбудить один ожидающий поток
    Logger::debug("ThreadPool Enqueue: Задача добавлена в очередь.");
    return true;
}

/*!
 * \brief Останавливает пул потоков.
 */
void ThreadPool::stop() {
    bool expected_stop_flag = false;
    // compare_exchange_strong гарантирует, что основная логика остановки выполнится только один раз
    if (!stop_initiated_.compare_exchange_strong(expected_stop_flag, true)) {
        Logger::info("ThreadPool Stop: Остановка уже была инициирована ранее или уже завершена.");
        // Можно добавить ожидание, если потоки еще не завершились, но это усложнит,
        // так как joinable() нужно проверять осторожно. Деструктор это обработает.
        return;
    }

    Logger::info("ThreadPool Stop: Инициирована остановка пула потоков...");
    // running_ также устанавливаем в false, чтобы потоки, которые могли пропустить notify_all,
    // вышли из цикла worker_thread_loop при следующей проверке running_.load() (хотя stop_initiated_ важнее).
    // running_ в основном для предотвращения enqueue после начала stop().
    running_.store(false); 

    // Разбудить все потоки, чтобы они проверили флаг stop_initiated_
    {
        std::lock_guard<std::mutex> lock(queue_mutex_); // Нужен для безопасного notify_all по той же condition
        condition_.notify_all();
    }
    

    Logger::debug("ThreadPool Stop: Ожидание завершения всех рабочих потоков (" + std::to_string(workers_.size()) + " потоков)...");
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            try {
                worker.join(); // Ожидаем завершения каждого потока
            } catch (const std::system_error& e) {
                Logger::error("ThreadPool Stop: Ошибка при join рабочего потока: " + std::string(e.what()));
                // Продолжаем попытки для остальных потоков
            }
        }
    }
    workers_.clear(); // Очищаем вектор после завершения всех потоков
    Logger::info("ThreadPool Stop: Все рабочие потоки завершены. Пул остановлен.");
}
