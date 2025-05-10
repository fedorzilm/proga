#include "gtest/gtest.h"
#include "common_defs.h" // Предполагается, что common_defs.h доступен через include_directories
#include <cstdio>       // Для std::remove в TearDown, если не используется filesystem
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>       // Для std::isdigit
#include <filesystem>   // Для std::filesystem (C++17)

// Вспомогательная функция для проверки существования директории (если не используется C++17 filesystem)
// bool directory_exists_stat(const std::string& path_str) {
//    struct stat info;
//    if (stat(path_str.c_str(), &info) != 0) return false; // Не удалось получить информацию
//    return (info.st_mode & S_IFDIR) != 0; // Это директория?
// }

// Используем std::filesystem::is_directory для C++17
bool directory_exists_fs(const std::string& path_str) {
    try {
        std::filesystem::path p(path_str);
        return std::filesystem::exists(p) && std::filesystem::is_directory(p);
    } catch (const std::filesystem::filesystem_error& e) {
        // std::cerr << "Filesystem error: " << e.what() << std::endl; // Для отладки
        return false;
    }
}


// Test fixture for tests that might need temporary files/directories
class CommonDefsFileTest : public ::testing::Test {
protected:
    // Используем std::filesystem::path для удобства работы с путями
    // Создаем временную директорию в системной временной папке для большей надежности
    const std::filesystem::path base_test_temp_dir_ = std::filesystem::temp_directory_path() / "ip_db_common_defs_tests";
    
    // temp_dir_name будет поддиректорией в base_test_temp_dir_
    const std::filesystem::path temp_dir_name_ = base_test_temp_dir_ / "test_subdir";
    const std::filesystem::path temp_file_in_dir_ = temp_dir_name_ / "test_file_unit.txt";
    const std::filesystem::path temp_file_alone_ = base_test_temp_dir_ / "temp_test_file_alone_unit.txt";


    void SetUp() override {
        // Очищаем и создаем базовую временную директорию перед каждым тестом
        try {
            if (std::filesystem::exists(base_test_temp_dir_)) {
                std::filesystem::remove_all(base_test_temp_dir_);
            }
            std::filesystem::create_directories(base_test_temp_dir_);
        } catch (const std::filesystem::filesystem_error& e) {
            FAIL() << "Setup failed: Could not manage base_test_temp_dir_: " << base_test_temp_dir_.string() << ". Error: " << e.what();
        }
    }

    void TearDown() override {
        // Очищаем базовую временную директорию после каждого теста
        try {
             if (std::filesystem::exists(base_test_temp_dir_)) {
                std::filesystem::remove_all(base_test_temp_dir_);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            // Логируем ошибку, но не проваливаем тест из-за ошибки в TearDown
            std::cerr << "Warning: TearDown failed to remove base_test_temp_dir_: " << base_test_temp_dir_.string() << ". Error: " << e.what() << std::endl;
        }
    }
};


TEST(CommonDefsTest, TrimString) {
    EXPECT_EQ(trim_string("  hello  "), "hello");
    EXPECT_EQ(trim_string("hello  "), "hello");
    EXPECT_EQ(trim_string("  hello"), "hello");
    EXPECT_EQ(trim_string("hello"), "hello");
    EXPECT_EQ(trim_string("  "), "");
    EXPECT_EQ(trim_string(""), "");
    EXPECT_EQ(trim_string("\t\n hello \v\f\r "), "hello");
    // Добавим тест с неразрывным пробелом UTF-8 (C2 A0), если trim_string его обрабатывает
    // Если ваш trim_string ориентирован только на ASCII пробелы, этот тест может упасть или его нужно адаптировать.
    // const char* utf8_nbsp = "\xC2\xA0"; // Это один символ UTF-8 неразрывного пробела
    // EXPECT_EQ(trim_string(std::string(utf8_nbsp) + "hello" + std::string(utf8_nbsp)), "hello");
    // Ваша реализация trim_string использует " \t\n\r\f\v\xC2\xA0", так что этот тест должен проходить.
    EXPECT_EQ(trim_string("\xC2\xA0hello\xC2\xA0"), "hello");

}

TEST(CommonDefsTest, ToLowerUtil) {
    EXPECT_EQ(to_lower_util("Hello World"), "hello world");
    EXPECT_EQ(to_lower_util("HELLO"), "hello");
    EXPECT_EQ(to_lower_util("hello"), "hello");
    EXPECT_EQ(to_lower_util("123 Hello"), "123 hello");
    EXPECT_EQ(to_lower_util(""), "");
    // Тест с кириллицей (std::tolower по умолчанию может не работать корректно с UTF-8 без настройки локали)
    // Если to_lower_util использует std::tolower(c, std::locale("...")), то это будет работать.
    // Если просто std::tolower(c), то для не-ASCII символов поведение не определено и зависит от текущей C локали.
    // Ваша реализация to_lower_util: static_cast<char>(std::tolower(c)) - зависит от C-локали.
    // EXPECT_EQ(to_lower_util("Привет МИР"), "привет мир"); // Этот тест может быть нестабильным
}

TEST(CommonDefsTest, IsValidSimpleFilename) {
    EXPECT_TRUE(is_valid_simple_filename("test.txt"));
    EXPECT_TRUE(is_valid_simple_filename("test_123-file.dat"));
    EXPECT_TRUE(is_valid_simple_filename("a.b-c_1"));
    EXPECT_FALSE(is_valid_simple_filename("data/subdir/file.cfg")); // Содержит /
    EXPECT_FALSE(is_valid_simple_filename("data\\subdir\\file.cfg"));// Содержит \
    EXPECT_FALSE(is_valid_simple_filename(""));
    EXPECT_FALSE(is_valid_simple_filename("test|file.txt"));    // Запрещенный символ
    EXPECT_FALSE(is_valid_simple_filename("my file.txt"));      // Пробел запрещен по вашей реализации
    EXPECT_FALSE(is_valid_simple_filename("файл.txt"));         // Не ASCII (is_valid_simple_filename_char проверяет isalnum)
    std::string long_name(255, 'a');                           // Максимальная длина простого имени
    EXPECT_TRUE(is_valid_simple_filename(long_name));
    std::string too_long_name(256, 'a');
    EXPECT_FALSE(is_valid_simple_filename(too_long_name));      // Слишком длинное имя
    EXPECT_FALSE(is_valid_simple_filename("file.txt "));        // Пробел в конце
    EXPECT_FALSE(is_valid_simple_filename(" file.txt"));        // Пробел в начале
    EXPECT_FALSE(is_valid_simple_filename(".."));               // ".." как имя файла
    EXPECT_FALSE(is_valid_simple_filename("file..txt"));        // ".." внутри имени
}

TEST(CommonDefsTest, IsValidCmdArgumentPath) {
    EXPECT_TRUE(is_valid_cmd_argument_path("test.txt"));
    EXPECT_TRUE(is_valid_cmd_argument_path("data/subdir/file.cfg"));
    EXPECT_TRUE(is_valid_cmd_argument_path("/home/user/docs/file with spaces.doc"));
    EXPECT_TRUE(is_valid_cmd_argument_path("data/Рабочий стол/файл.txt")); // Кириллица должна проходить

    EXPECT_FALSE(is_valid_cmd_argument_path("")); // Пустой путь
    
    char path_with_nul_array[] = {'t', 'e', 's', 't', '\0', 'f', 'i', 'l', 'e'};
    std::string nul_path_str_from_array(path_with_nul_array, sizeof(path_with_nul_array)); 
    EXPECT_FALSE(is_valid_cmd_argument_path(nul_path_str_from_array)); // Содержит NUL

    std::string nul_path_constructed = std::string("test");
    nul_path_constructed += '\0';
    nul_path_constructed += "file.txt";
    EXPECT_FALSE(is_valid_cmd_argument_path(nul_path_constructed)); // Содержит NUL

    std::string too_long_path(4097, 'a'); // PATH_MAX + 1 (у вас лимит 4096)
    EXPECT_FALSE(is_valid_cmd_argument_path(too_long_path)); // Слишком длинный путь
    EXPECT_FALSE(is_valid_cmd_argument_path("path/with\x01control_char.txt")); // SOH (0x01)
    EXPECT_TRUE(is_valid_cmd_argument_path("path/with\x09tab.txt"));      // TAB (0x09) разрешен
    EXPECT_FALSE(is_valid_cmd_argument_path("path/with\x0Bvtab.txt"));     // VTAB (0x0B) - ваша функция может его запрещать, если не явно разрешен
                                                                          // Ваша функция разрешает \t \n \r \f \v. Так что VTAB должен быть TRUE.
    EXPECT_TRUE(is_valid_cmd_argument_path("path/with\x0B_vtab.txt")); 
    EXPECT_FALSE(is_valid_cmd_argument_path("path/with\x7Fdel_char.txt"));   // DEL (0x7F)
}


TEST_F(CommonDefsFileTest, EnsureDirectoryExistsUtilForFileInNewDir) { 
    std::filesystem::path file_path = temp_dir_name_ / "test_file.txt";
    ensure_directory_exists_util(file_path.string()); // Должен создать temp_dir_name_
    
    EXPECT_TRUE(directory_exists_fs(temp_dir_name_.string())) << "Directory " << temp_dir_name_.string() << " was not created.";
    EXPECT_FALSE(std::filesystem::exists(file_path)) << "File " << file_path.string() << " should NOT have been created.";
}

TEST_F(CommonDefsFileTest, EnsureDirectoryExistsUtilNestedSubdirs) {
    std::filesystem::path nested_file_path = temp_dir_name_ / "sub1" / "sub2" / "file.txt";
    ensure_directory_exists_util(nested_file_path.string()); // Должен создать temp_dir_name_/sub1/sub2
    
    EXPECT_TRUE(directory_exists_fs(temp_dir_name_ / "sub1" / "sub2")) << "Nested directory " 
        << (temp_dir_name_ / "sub1" / "sub2").string() << " not created.";
    EXPECT_FALSE(std::filesystem::exists(nested_file_path)) << "File in nested directory should not have been created.";
}

TEST_F(CommonDefsFileTest, EnsureDirectoryExistsUtilWithDotInPath) {
    // Путь: base_test_temp_dir_ / . / sub_dot / file.txt -> должен создать base_test_temp_dir_ / sub_dot
    std::filesystem::path path_with_dot = base_test_temp_dir_ / "." / "sub_dot" / "file.txt";
    ensure_directory_exists_util(path_with_dot.string());

    EXPECT_TRUE(directory_exists_fs(base_test_temp_dir_ / "sub_dot")) << "Directory " 
        << (base_test_temp_dir_ / "sub_dot").string() << " not created with '.' in path.";
    EXPECT_FALSE(std::filesystem::exists(path_with_dot));
}

TEST_F(CommonDefsFileTest, EnsureDirectoryExistsUtilWithDotDotInPath) {
    std::filesystem::path initial_subdir = base_test_temp_dir_ / "initial_sub";
    std::filesystem::create_directories(initial_subdir); 
    ASSERT_TRUE(directory_exists_fs(initial_subdir.string()));

    // Путь: base_test_temp_dir_/initial_sub/../target_sub/file.txt 
    // -> должен создать base_test_temp_dir_/target_sub
    std::filesystem::path path_with_dot_dot = initial_subdir / ".." / "target_sub" / "file.txt";
    ensure_directory_exists_util(path_with_dot_dot.string());
    
    std::filesystem::path expected_target_dir = base_test_temp_dir_ / "target_sub";
    EXPECT_TRUE(directory_exists_fs(expected_target_dir.string())) << "Directory " 
        << expected_target_dir.string() << " not created with '..' in path.";
    EXPECT_FALSE(std::filesystem::exists(path_with_dot_dot));
}


TEST_F(CommonDefsFileTest, EnsureDirectoryExistsUtilNoDirectoryInPath) {
    // Используем файл temp_file_alone_, который находится в base_test_temp_dir_
    // ensure_directory_exists_util(temp_file_alone_.string()); // temp_file_alone_ - это имя файла, а не директории.
    // Эта функция пытается извлечь путь к директории из полного пути к файлу.
    // Если путь "просто_имя_файла.txt", то last_slash_idx == npos, и path_to_check будет пуст, функция вернет управление.
    
    ensure_directory_exists_util(temp_file_alone_.string());

    // Директория base_test_temp_dir_ должна существовать из SetUp
    ASSERT_TRUE(directory_exists_fs(base_test_temp_dir_.string()));
    // Файл не должен быть создан
    EXPECT_FALSE(std::filesystem::exists(temp_file_alone_)) << "File " << temp_file_alone_.string() 
        << " should not exist (not created by ensure_directory_exists_util).";
}

TEST(CommonDefsTest, GetCurrentTimestampFormat) {
    std::string ts = get_current_timestamp();
    // Формат: YYYY-MM-DD HH:MM:SS.mmm (23 символа)
    ASSERT_EQ(ts.length(), 23); 
    EXPECT_EQ(ts[4], '-');
    EXPECT_EQ(ts[7], '-');
    EXPECT_EQ(ts[10], ' ');
    EXPECT_EQ(ts[13], ':');
    EXPECT_EQ(ts[16], ':');
    EXPECT_EQ(ts[19], '.');
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18, 20, 21, 22}) {
        EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(ts[i]))) 
            << "Timestamp char at index " << i << " ('" << ts[i] << "') is not a digit in: " << ts;
    }
}

TEST(CommonDefsTest, TrafficReadingTotal) {
    TrafficReading tr;
    EXPECT_EQ(tr.incoming, 0);
    EXPECT_EQ(tr.outgoing, 0);
    EXPECT_EQ(tr.total(), 0);

    tr.incoming = 100;
    tr.outgoing = 50;
    EXPECT_EQ(tr.total(), 150);

    // Проверка с большими числами (но без переполнения long long для total)
    tr.incoming = 1000000000000LL; // 10^12
    tr.outgoing = 500000000000LL;  // 0.5 * 10^12
    EXPECT_EQ(tr.total(), 1500000000000LL);
}
