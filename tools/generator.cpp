#include "../src/provider_record.h"
#include "../src/common_defs.h"    // Содержит is_valid_cmd_argument_path и is_valid_simple_filename
#include "../src/ip_address.h"
#include "../src/date.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <stdexcept>
#include <cstdlib>   // Для system()
#include <locale>    // Для std::locale в main

// Используем ensure_directory_exists_util из common_defs.h
// void ensure_output_directory_exists_gen_final(const std::string& path_str) { ... }

const std::vector<std::string> sample_first_names_gen = {
    "Alice", "Bob", "Charlie", "David", "Eve", "Frank", "Grace", "Heidi",
    "Ivan", "Judy", "Kevin", "Linda", "Michael", "Nancy", "Oscar", "Patricia",
    "Quentin", "Rachel", "Samuel", "Tina", "Ulysses", "Violet", "Walter", "Xenia", "Yvonne", "Zachary"
};
const std::vector<std::string> sample_last_names_gen = {
    "Smith", "Jones", "Williams", "Brown", "Davis", "Miller", "Wilson", "Moore",
    "Taylor", "Anderson", "Thomas", "Jackson", "White", "Harris", "Martin", "Thompson",
    "Garcia", "Martinez", "Robinson", "Clark", "Rodriguez", "Lewis", "Lee", "Walker", "Hall", "Allen"
};

std::string generate_random_ip_str_gen(std::mt19937& gen) {
    std::uniform_int_distribution<> distrib(1, 254);
    std::uniform_int_distribution<> first_octet_dist(1, 223); // A, B, C классы, исключая 0.x.x.x и 224+
    return std::to_string(first_octet_dist(gen)) + "." +
           std::to_string(distrib(gen)) + "." +
           std::to_string(distrib(gen)) + "." +
           std::to_string(distrib(gen));
}

Date generate_random_date_obj_gen(std::mt19937& gen, int start_year = 2023, int end_year = 2024) {
    std::uniform_int_distribution<> year_dist(start_year, end_year);
    std::uniform_int_distribution<> month_dist(1, 12);

    int year = year_dist(gen);
    int month = month_dist(gen);
    int max_day = Date::days_in_month(year, month);
    if (max_day == 0) max_day = 28; // Fallback для безопасности, хотя days_in_month должен вернуть корректное значение
    std::uniform_int_distribution<> day_dist(1, max_day);
    int day = day_dist(gen);

    return Date(year, month, day);
}

int main(int argc, char* argv[]) {
    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error& e) {
        std::cerr << "Warning: Could not set global locale: " << e.what() << ". Trying C.UTF-8" << std::endl;
        try { std::locale::global(std::locale("C.UTF-8"));} catch (const std::runtime_error& e2){
             std::cerr << "Warning: Could not set C.UTF-8 locale: " << e2.what() << std::endl;
        }
    }


    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <number_of_records> [output_filepath (default: data/provider_data.txt)]" << std::endl;
        return 1;
    }

    int num_records = 0;
    try {
        num_records = std::stoi(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid number of records '" << argv[1] << "': " << e.what() << std::endl;
        return 1;
    }

    if (num_records <= 0) {
        std::cerr << "Error: Number of records must be positive." << std::endl;
        return 1;
    }

    std::string output_filename_gen = "data/provider_data.txt";
    if (argc > 2) {
        output_filename_gen = argv[2];
    }

    // Если output_filename_gen - это полный путь, используем is_valid_cmd_argument_path.
    // Если это простое имя файла, которое будет создано в ./data/ или текущей директории,
    // то is_valid_simple_filename может быть уместнее.
    // Так как argv[2] может быть чем угодно, is_valid_cmd_argument_path безопаснее.
    if (!is_valid_cmd_argument_path(output_filename_gen)) { // <--- ИЗМЕНЕНИЕ
        std::cerr << "Error: Invalid output filepath: " << output_filename_gen << std::endl;
        return 1;
    }

    ensure_directory_exists_util(output_filename_gen); // Создать директорию, если путь указан

    std::ofstream ofs(output_filename_gen);
    if (!ofs) {
        std::cerr << "Error: Cannot open output file '" << output_filename_gen << "' for writing." << std::endl;
        return 1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> first_name_idx_dist(0, static_cast<int>(sample_first_names_gen.size() - 1));
    std::uniform_int_distribution<> last_name_idx_dist(0, static_cast<int>(sample_last_names_gen.size() - 1));
    std::uniform_int_distribution<long long> traffic_val_dist(0, 1000000); // Примерный диапазон трафика

    std::cout << "Generating " << num_records << " records (with IN/OUT traffic) into '" << output_filename_gen << "'..." << std::endl;
    int valid_records_generated_count = 0;

    for (int i = 0; i < num_records; ++i) {
        ProviderRecord rec;
        rec.full_name = sample_first_names_gen[first_name_idx_dist(gen)] + " " + sample_last_names_gen[last_name_idx_dist(gen)];

        auto ip_opt = IpAddress::from_string(generate_random_ip_str_gen(gen));
        if (!ip_opt) {
            std::cerr << "Warning: Generator produced invalid IP, skipping record " << i+1 << std::endl;
            continue; // Пропустить эту запись
        }
        rec.ip_address = *ip_opt;
        rec.record_date = generate_random_date_obj_gen(gen);

        rec.hourly_traffic.resize(HOURS_IN_DAY);
        for (size_t h = 0; h < HOURS_IN_DAY; ++h) {
            rec.hourly_traffic[h].incoming = traffic_val_dist(gen);
            rec.hourly_traffic[h].outgoing = traffic_val_dist(gen);
        }

        if (rec.is_valid()) { // is_valid() должен быть надежным
            rec.write(ofs);
            valid_records_generated_count++;
        } else {
            std::cerr << "Warning: Generated invalid record data for record " << i+1 << ". Skipping." << std::endl;
        }

        if ((i + 1) % (num_records / 20 < 1 ? 1 : num_records / 20) == 0 && num_records >=20 && (i+1) != num_records ) {
            std::cout << "Processed " << (i + 1) << " records (generated " << valid_records_generated_count << " valid)..." << std::endl;
        }
    }
    std::cout << "Generation complete. " << valid_records_generated_count << " valid records written to '" << output_filename_gen << "'." << std::endl;

    if (!ofs.good()) { // Проверка после завершения цикла записи
        std::cerr << "Error occurred during file writing to '" << output_filename_gen << "'." << std::endl;
        ofs.close();
        return 1;
    }
    ofs.close();
    if (ofs.bad()){ // Проверка после закрытия
         std::cerr << "Error occurred after closing file '" << output_filename_gen << "'." << std::endl;
        return 1;
    }
    return 0;
}

