#include "gtest/gtest.h"
#include "utils/file_utils.h" // Включает common_defs.h (и filesystem)
#include "utils/logger.h"     
#include <fstream>
#include <filesystem> // Для std::filesystem
#include <iostream>   // Для std::cerr, std::cout и std::boolalpha в SetUp/TearDown и тестах
#include <random>     // Для std::mt19937, std::uniform_int_distribution
#include <chrono>     // Для std::chrono (для seed генератора)
#include <cstdlib>    // Не используется напрямую, но оставим на всякий случай

#ifdef _WIN32
#include <direct.h>
#define GETCWD _getcwd
#else
#include <unistd.h>
#define GETCWD getcwd
#endif

// Вспомогательная функция для создания структуры директорий для теста
void create_test_directory_structure(const std::filesystem::path& base_path, const std::string& structure_type) {
    std::error_code ec;
    std::filesystem::create_directories(base_path, ec);
    if (ec) {
        std::cerr << "Warning: Could not create base_path in create_test_directory_structure: " << base_path << " Error: " << ec.message() << std::endl;
        return;
    }

    if (structure_type == "project_with_cmake_and_src") {
        std::filesystem::create_directories(base_path / "src", ec);
        std::filesystem::create_directories(base_path / "build" / "bin", ec);
        std::ofstream cmake_list(base_path / "CMakeLists.txt");
        if (cmake_list.is_open()) {
            cmake_list << "# Test CMakeLists.txt" << std::endl;
            cmake_list.close();
        } else {
            std::cerr << "Warning: Could not create CMakeLists.txt in " << base_path << std::endl;
        }
    } else if (structure_type == "project_with_git") {
        std::filesystem::create_directories(base_path / ".git", ec);
        std::filesystem::create_directories(base_path / "app", ec);
    } else if (structure_type == "simple_dir") {
        std::filesystem::create_directories(base_path / "some_subdir", ec);
    }
     std::filesystem::create_directories(base_path / DEFAULT_SERVER_DATA_SUBDIR, ec); // DEFAULT_SERVER_DATA_SUBDIR из common_defs.h

    if (ec) {
         std::cerr << "Warning: create_directories failed with error: " << ec.message() << " for path part in create_test_directory_structure" << std::endl;
    }
}

class FileUtilsTest : public ::testing::Test {
protected:
    std::filesystem::path test_base_dir;
    std::filesystem::path original_cwd;
    std::filesystem::path system_temp_dir;


    void SetUp() override {

        char buffer[FILENAME_MAX];
        if (GETCWD(buffer, FILENAME_MAX) != nullptr) {
            original_cwd = buffer;
        } else {
            int getcwd_errno = errno; 
            std::cerr << "ПРЕДУПРЕЖДЕНИЕ FileUtilsTest::SetUp: Не удалось получить текущую рабочую директорию через GETCWD. Errno: "
                      << getcwd_errno << " (" << strerror(getcwd_errno) << ")." << std::endl;
            try {
                original_cwd = std::filesystem::current_path();
            } catch (const std::filesystem::filesystem_error& e) {
                std::cerr << "КРИТИЧЕСКАЯ ОШИБКА FileUtilsTest::SetUp: std::filesystem::current_path() также не удалось: " << e.what()
                          << ". original_cwd останется пустым." << std::endl;
                original_cwd.clear();
            }
        }

        std::error_code ec_temp_path;
        system_temp_dir = std::filesystem::temp_directory_path(ec_temp_path);

        if (ec_temp_path || system_temp_dir.empty()) {
            std::cerr << "КРИТИЧЕСКАЯ ОШИБКА FileUtilsTest::SetUp: Не удалось получить системную временную директорию."
                      << " Ошибка: " << ec_temp_path.message()
                      << ". Будет использована локальная временная директория 'test_temp_fu' в CWD: "
                      << (original_cwd.empty() ? "Неизвестный CWD" : original_cwd.string()) << std::endl;
            if (!original_cwd.empty() && std::filesystem::exists(original_cwd)) {
                 test_base_dir = original_cwd / "test_temp_fu_";
            } else {
                 test_base_dir = "test_temp_fu_"; 
            }
        } else {
            test_base_dir = system_temp_dir / "fu_test_";
        }

        unsigned int seed = static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count());
        std::mt19937 generator(seed);
        std::uniform_int_distribution<int> distribution(0, 999999);
        test_base_dir += std::to_string(distribution(generator));

        std::error_code ec_remove;
        std::filesystem::remove_all(test_base_dir, ec_remove);
        if (ec_remove) {
             std::cerr << "Предупреждение FileUtilsTest::SetUp: Не удалось удалить старую test_base_dir '" << test_base_dir
                       << "'. Ошибка: " << ec_remove.message() << std::endl;
        }

        std::error_code ec_create_dirs;
        if (!std::filesystem::create_directories(test_base_dir, ec_create_dirs)) {
             if (ec_create_dirs) {
                std::cerr << "КРИТИЧЕСКАЯ ОШИБКА FileUtilsTest::SetUp: Не удалось создать базовую тестовую директорию: "
                          << test_base_dir.string() << ". Ошибка: " << ec_create_dirs.message() << std::endl;
             } else if (!std::filesystem::exists(test_base_dir)) {
                 std::cerr << "КРИТИЧЕСКАЯ ОШИБКА FileUtilsTest::SetUp: Не удалось создать базовую тестовую директорию: "
                           << test_base_dir.string() << ", и она не существует." << std::endl;
             }
        }
        if (!(std::filesystem::exists(test_base_dir) && std::filesystem::is_directory(test_base_dir))) {
             std::cerr << "FileUtilsTest::SetUp: Повторная попытка создания тестовой директории локально." << std::endl;
             test_base_dir = "local_fu_test_fallback_";
             test_base_dir += std::to_string(distribution(generator));
             std::filesystem::remove_all(test_base_dir); 
             std::filesystem::create_directories(test_base_dir);
        }
        ASSERT_TRUE(std::filesystem::exists(test_base_dir) && std::filesystem::is_directory(test_base_dir))
            << "Не удалось создать базовую тестовую директорию: " << test_base_dir.string();
    }

    void TearDown() override {
        if (!test_base_dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(test_base_dir, ec);
            if (ec) {
                 std::cerr << "Предупреждение FileUtilsTest::TearDown: Ошибка при удалении тестовой директории "
                           << test_base_dir << ". Ошибка: " << ec.message() << std::endl;
            }
        } else {
            std::cerr << "Предупреждение FileUtilsTest::TearDown: test_base_dir пуст, удаление не требуется." << std::endl;
        }
    }

    std::filesystem::path callGetProjectRootPath(const std::filesystem::path& p) {
        if (p.empty()) {
            return FileUtils::getProjectRootPath(".");
        }
        return FileUtils::getProjectRootPath(p.string().c_str());
    }
    std::filesystem::path callGetProjectDataPath(const std::string& subdir_file, const std::filesystem::path& exec_path_for_root) {
         if (exec_path_for_root.empty()) {
            return FileUtils::getProjectDataPath(subdir_file, ".");
        }
        return FileUtils::getProjectDataPath(subdir_file, exec_path_for_root.string().c_str());
    }
};

TEST_F(FileUtilsTest, GetProjectRootPath_SimpleHeuristic_BuildBin) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path project_root_sim = test_base_dir / "myproject_bb";
    create_test_directory_structure(project_root_sim, "project_with_cmake_and_src");
    std::filesystem::path executable_path = project_root_sim / "build" / "bin" / "server";
    std::filesystem::create_directories(executable_path.parent_path()); 

    std::filesystem::path found_root = callGetProjectRootPath(executable_path);
    EXPECT_EQ(std::filesystem::weakly_canonical(found_root), std::filesystem::weakly_canonical(project_root_sim));
}

TEST_F(FileUtilsTest, GetProjectRootPath_MarkerCMakeLists) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path project_root_sim = test_base_dir / "myproject_cmake";
    create_test_directory_structure(project_root_sim, "project_with_cmake_and_src");
    std::filesystem::path path_inside_project = project_root_sim / "src" / "main.cpp"; 

    std::filesystem::path found_root = callGetProjectRootPath(path_inside_project);
    EXPECT_EQ(std::filesystem::weakly_canonical(found_root), std::filesystem::weakly_canonical(project_root_sim));
}

TEST_F(FileUtilsTest, GetProjectRootPath_MarkerGit) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path project_root_sim = test_base_dir / "myproject_git";
    create_test_directory_structure(project_root_sim, "project_with_git");
    std::filesystem::path path_inside_project = project_root_sim / "app" / "start.sh";

    std::filesystem::path found_root = callGetProjectRootPath(path_inside_project);
    EXPECT_EQ(std::filesystem::weakly_canonical(found_root), std::filesystem::weakly_canonical(project_root_sim));
}

TEST_F(FileUtilsTest, GetProjectRootPath_FallbackToCWD_WhenNoMarkersOrStructure) {
    if (test_base_dir.empty() || original_cwd.empty()) GTEST_SKIP() << "test_base_dir или original_cwd не инициализирован, пропускаем тест.";
    std::filesystem::path non_project_dir = test_base_dir / "not_a_project";
    create_test_directory_structure(non_project_dir, "simple_dir");
    std::filesystem::path executable_path_in_subdir = non_project_dir / "some_subdir" / "program";
    std::filesystem::create_directories(executable_path_in_subdir.parent_path());


    std::filesystem::path old_cwd_for_test = std::filesystem::current_path();
    std::error_code ec_set_cwd;
    std::filesystem::current_path(non_project_dir, ec_set_cwd);
    if (ec_set_cwd) {
        std::filesystem::current_path(old_cwd_for_test, ec_set_cwd); 
        GTEST_SKIP() << "Could not change CWD for test.";
    }

    std::filesystem::path found_root = callGetProjectRootPath(executable_path_in_subdir);
    
    EXPECT_EQ(std::filesystem::weakly_canonical(found_root), std::filesystem::weakly_canonical(non_project_dir));

    std::filesystem::current_path(old_cwd_for_test, ec_set_cwd); 
}

TEST_F(FileUtilsTest, GetProjectRootPath_EmptyOrNullPath) {
    EXPECT_THROW(FileUtils::getProjectRootPath(nullptr), std::runtime_error);
    EXPECT_THROW(FileUtils::getProjectRootPath(""), std::runtime_error);
}

TEST_F(FileUtilsTest, GetProjectDataPath_Basic) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path project_root_sim = test_base_dir / "myproject_dp";
    create_test_directory_structure(project_root_sim, "project_with_cmake_and_src"); 
    std::filesystem::path executable_path = project_root_sim / "build" / "bin" / "server";
     std::filesystem::create_directories(executable_path.parent_path());


    std::filesystem::path expected_data_path = project_root_sim / "data" / "myfile.txt";
    std::filesystem::path found_data_path = callGetProjectDataPath("myfile.txt", executable_path);
    
    EXPECT_TRUE(std::filesystem::exists(project_root_sim / "data")) << "Директория data должна быть создана функцией getProjectDataPath";
    EXPECT_EQ(std::filesystem::weakly_canonical(found_data_path), std::filesystem::weakly_canonical(expected_data_path));
}

TEST_F(FileUtilsTest, GetProjectDataPath_OnlyDataDir) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path project_root_sim = test_base_dir / "myproject_dp_only";
    create_test_directory_structure(project_root_sim, "project_with_cmake_and_src");
    std::filesystem::path executable_path = project_root_sim / "build" / "bin" / "server";
    std::filesystem::create_directories(executable_path.parent_path());

    std::filesystem::path expected_data_dir = project_root_sim / "data";
    std::filesystem::path found_data_dir = callGetProjectDataPath("", executable_path);

    EXPECT_TRUE(std::filesystem::exists(project_root_sim / "data"));
    EXPECT_EQ(std::filesystem::weakly_canonical(found_data_dir), std::filesystem::weakly_canonical(expected_data_dir));
}

TEST_F(FileUtilsTest, GetSafeServerFilePath_ValidFilename) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path server_root = test_base_dir / "server_files_root";
    std::filesystem::create_directories(server_root); 
    
    std::string subdir_name = DEFAULT_SERVER_DATA_SUBDIR; // Из common_defs.h

    std::string client_filename = "mydb.dat";
    std::filesystem::path safe_path = FileUtils::getSafeServerFilePath(server_root.string(), client_filename, subdir_name);
    
    std::filesystem::path expected_full_path = server_root / subdir_name / client_filename;
    EXPECT_TRUE(std::filesystem::exists(server_root / subdir_name)) << "Поддиректория данных должна быть создана";
    EXPECT_EQ(std::filesystem::weakly_canonical(safe_path), std::filesystem::weakly_canonical(expected_full_path));
}

TEST_F(FileUtilsTest, GetSafeServerFilePath_FilenameCleaning) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path server_root = test_base_dir / "server_files_root_clean";
    std::filesystem::create_directories(server_root);
    std::string subdir_name = DEFAULT_SERVER_DATA_SUBDIR; // Из common_defs.h

    std::string client_filename_traverse = "../outside_db.txt"; 
    std::filesystem::path safe_path_traverse = FileUtils::getSafeServerFilePath(server_root.string(), client_filename_traverse, subdir_name);
    std::filesystem::path expected_path_traverse = server_root / subdir_name / "outside_db.txt"; 
    EXPECT_EQ(std::filesystem::weakly_canonical(safe_path_traverse), std::filesystem::weakly_canonical(expected_path_traverse));

    EXPECT_THROW(FileUtils::getSafeServerFilePath(server_root.string(), "fi*le.txt", subdir_name), std::runtime_error);
    EXPECT_THROW(FileUtils::getSafeServerFilePath(server_root.string(), "fi:le.txt", subdir_name), std::runtime_error);
    
    std::filesystem::path safe_path_slashes = FileUtils::getSafeServerFilePath(server_root.string(), "file/with/slashes.txt", subdir_name);
    std::filesystem::path expected_slashes = server_root / subdir_name / "slashes.txt";
    EXPECT_EQ(std::filesystem::weakly_canonical(safe_path_slashes), std::filesystem::weakly_canonical(expected_slashes));
    
    std::string filename_with_ctrl_chars = ".\x01.\x0Ftest.db";
    std::filesystem::path safe_path_ctrl = FileUtils::getSafeServerFilePath(server_root.string(), filename_with_ctrl_chars, subdir_name);
    std::filesystem::path expected_path_ctrl = server_root / subdir_name / "test.db"; 
    EXPECT_EQ(std::filesystem::weakly_canonical(safe_path_ctrl), std::filesystem::weakly_canonical(expected_path_ctrl));
}

TEST_F(FileUtilsTest, GetSafeServerFilePath_PathTraversalAttempt_ShouldCorrect) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path server_root = test_base_dir / "server_files_root_trav";
    std::filesystem::create_directories(server_root);
    std::string subdir_name = "data_subdir_trav"; 
    
    std::ofstream outside_file(server_root / "secret.txt");
    ASSERT_TRUE(outside_file.is_open());
    outside_file << "secret data";
    outside_file.close();
    ASSERT_TRUE(std::filesystem::exists(server_root / "secret.txt"));

    std::filesystem::path path1;
    EXPECT_NO_THROW(path1 = FileUtils::getSafeServerFilePath(server_root.string(), "../secret.txt", subdir_name));
    EXPECT_EQ(std::filesystem::weakly_canonical(path1), std::filesystem::weakly_canonical(server_root / subdir_name / "secret.txt"));

    std::string tricky_filename_internal_dots = std::string("dummy_dir") + 
                                  std::filesystem::path::preferred_separator + ".." + 
                                  std::filesystem::path::preferred_separator + ".." + 
                                  std::filesystem::path::preferred_separator + "another_secret.txt";
    std::filesystem::path path_tricky_corrected;
    EXPECT_NO_THROW(path_tricky_corrected = FileUtils::getSafeServerFilePath(server_root.string(), tricky_filename_internal_dots, subdir_name));
    EXPECT_EQ(std::filesystem::weakly_canonical(path_tricky_corrected), std::filesystem::weakly_canonical(server_root / subdir_name / "another_secret.txt"));


    std::filesystem::path absolute_outside_path = server_root / "absolute_secret.txt";
    std::ofstream abs_outside_file(absolute_outside_path);
    abs_outside_file << "absolute data";
    abs_outside_file.close();
    ASSERT_TRUE(std::filesystem::exists(absolute_outside_path));
    
    std::filesystem::path path_abs;
    EXPECT_NO_THROW(path_abs = FileUtils::getSafeServerFilePath(server_root.string(), absolute_outside_path.string(), subdir_name));
    EXPECT_EQ(std::filesystem::weakly_canonical(path_abs), std::filesystem::weakly_canonical(server_root / subdir_name / "absolute_secret.txt"));
}

TEST_F(FileUtilsTest, GetSafeServerFilePath_EmptyServerRoot_UsesCWDHeuristic) {
    if (original_cwd.empty()) GTEST_SKIP() << "original_cwd не инициализирован, пропускаем тест.";
    std::string client_filename = "test_in_cwd_root.db";
    std::string subdir_name_to_delete = "test_data_files_cwd_heuristic"; 

    std::filesystem::path safe_path;
    
    EXPECT_NO_THROW(safe_path = FileUtils::getSafeServerFilePath("", client_filename, subdir_name_to_delete));
    ASSERT_FALSE(safe_path.empty());
    EXPECT_EQ(safe_path.filename().string(), client_filename);
    ASSERT_TRUE(safe_path.has_parent_path());
    EXPECT_EQ(safe_path.parent_path().filename().string(), subdir_name_to_delete);
    
    EXPECT_TRUE(std::filesystem::exists(safe_path.parent_path())); 
    EXPECT_TRUE(std::filesystem::is_directory(safe_path.parent_path()));
    
    // Код очистки
    std::filesystem::path dir_path_to_delete = safe_path.parent_path(); 

    std::error_code ec_remove;
    if (std::filesystem::exists(dir_path_to_delete)) {
        std::filesystem::remove_all(dir_path_to_delete, ec_remove);
        if (ec_remove) {
            std::cerr << "Warning FileUtilsTest: std::filesystem::remove_all FAILED to delete '" << dir_path_to_delete.string()
                      << "'. Error Code: " << ec_remove.value() << ", Message: " << ec_remove.message() << std::endl;
        }     
    }     
}

TEST_F(FileUtilsTest, GetSafeServerFilePath_EmptyClientFilename) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path server_root = test_base_dir / "server_files_root_empty_client";
    std::filesystem::create_directories(server_root);
    EXPECT_THROW(FileUtils::getSafeServerFilePath(server_root.string(), "", DEFAULT_SERVER_DATA_SUBDIR), std::runtime_error);
    EXPECT_THROW(FileUtils::getSafeServerFilePath(server_root.string(), ".", DEFAULT_SERVER_DATA_SUBDIR), std::runtime_error); 
    EXPECT_THROW(FileUtils::getSafeServerFilePath(server_root.string(), "..", DEFAULT_SERVER_DATA_SUBDIR), std::runtime_error); 
}

TEST_F(FileUtilsTest, GetSafeServerFilePath_TooLongFilename) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path server_root = test_base_dir / "server_files_root_long";
     std::filesystem::create_directories(server_root);
    std::string long_name(300, 'a'); 
    EXPECT_THROW(FileUtils::getSafeServerFilePath(server_root.string(), long_name + ".txt", DEFAULT_SERVER_DATA_SUBDIR), std::runtime_error); 
}

TEST_F(FileUtilsTest, GetSafeServerFilePath_SubdirectoryCreation) {
    if (test_base_dir.empty()) GTEST_SKIP() << "test_base_dir не инициализирован, пропускаем тест.";
    std::filesystem::path server_root = test_base_dir / "sfsfp_subdir_creation";
    std::filesystem::create_directories(server_root);
    std::string subdir = "my_unique_data_subdir_creation_test"; 
    std::filesystem::path expected_full_subdir = server_root / subdir;

    ASSERT_FALSE(std::filesystem::exists(expected_full_subdir));
    
    std::filesystem::path safe_file_path;
    EXPECT_NO_THROW(safe_file_path = FileUtils::getSafeServerFilePath(server_root.string(), "testfile.db", subdir));
    
    EXPECT_TRUE(std::filesystem::exists(expected_full_subdir));
    EXPECT_TRUE(std::filesystem::is_directory(expected_full_subdir));
    EXPECT_EQ(std::filesystem::weakly_canonical(safe_file_path), std::filesystem::weakly_canonical(expected_full_subdir / "testfile.db"));
}
