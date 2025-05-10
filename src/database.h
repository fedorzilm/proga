#ifndef DATABASE_H
#define DATABASE_H

#include "provider_record.h"
#include "tariff_plan.h"
#include "query_parser.h" 
#include <vector>
#include <string>
#include <optional>
#include <mutex>     
#include <shared_mutex> 

class Database {
public:
    Database() = default;

    bool load_from_file(const std::string& filename);
    bool save_to_file(const std::string& filename) const; 

    bool add_record(ProviderRecord record);
    size_t delete_records(const SelectQuery& query);
    
    ProviderRecord* find_exact_record_for_edit(const std::string& name, const IpAddress& ip, const Date& date); 

    // НОВЫЙ МЕТОД для обновления записи с проверкой конфликтов
    // original_key_name, original_key_ip, original_key_date - для поиска записи
    // new_record_data - полная запись с новыми данными
    // Возвращает:
    //  0 - успех
    // -1 - запись для редактирования не найдена
    // -2 - новые идентификационные поля конфликтуют с другой существующей записью
    // -3 - обновленная запись невалидна
    int update_record(const std::string& original_key_name, 
                      const IpAddress& original_key_ip, 
                      const Date& original_key_date, 
                      const ProviderRecord& new_record_data);

    std::vector<ProviderRecord> select_records(const SelectQuery& query) const; 

    std::optional<double> calculate_bill(const SelectQuery& query, const Date& start_date, const Date& end_date, const TariffPlan& tariff) const;

    size_t record_count() const;
    void print_all_records_to_stream(std::ostream& os, size_t limit = 0) const; 

    std::vector<std::vector<std::string>> get_formatted_select_results(const SelectQuery& query) const;

private:
    std::vector<ProviderRecord> records;
    mutable std::shared_mutex db_mutex_; 
};

#endif // DATABASE_H
