#pragma once
#include <stdexcept>
#include <string>

namespace uORM {

/**
 * @brief Base class for all uORM exceptions
 */
class Exception : public std::exception {
public:
    explicit Exception(const std::string& msg) : msg_(msg) {}
    
    const char* what() const noexcept override {
        return msg_.c_str();
    }

protected:
    std::string msg_;
};

/**
 * @brief Exception thrown when configuration loading or parsing fails
 */
class ConfigurationError : public Exception {
public:
    using Exception::Exception;
};

/**
 * @brief Base class for database-related errors
 */
class DatabaseError : public Exception {
public:
    using Exception::Exception;
};

/**
 * @brief Exception thrown when connection fails
 */
class ConnectionError : public DatabaseError {
public:
    using DatabaseError::DatabaseError;
};

/**
 * @brief Exception thrown when SQL execution fails
 */
class SqlError : public DatabaseError {
public:
    using DatabaseError::DatabaseError;
};

/**
 * @brief Exception thrown for ORM mapping or reflection errors
 */
class OrmError : public Exception {
public:
    using Exception::Exception;
};

} // namespace uORM
