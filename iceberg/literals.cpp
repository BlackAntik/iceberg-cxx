#include "iceberg/literals.h"

#include <iostream>

#include "arrow/api.h"
#include "arrow/array/util.h"
#include "arrow/util/value_parsing.h"

namespace iceberg {
namespace {

void Ensure(bool cond, const std::string& message) {
  if (!cond) {
    throw std::runtime_error(message);
  }
}

int64_t ParseTime(const rapidjson::Value& document, arrow::TimeUnit::type type) {
  Ensure(document.IsString(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");
  int64_t dat;
  auto parser = arrow::TimestampParser::MakeISO8601();
  Ensure(parser->operator()(document.GetString(), document.GetStringLength(), type, &dat),
         std::string(__PRETTY_FUNCTION__) + ": Failed to parse Date/Timestamp");
  return dat;
}

std::shared_ptr<arrow::Scalar> GetScalar(std::shared_ptr<const types::Type> type, const rapidjson::Value& document) {
  switch (type->TypeId()) {
    case TypeID::kBoolean:
      return std::make_shared<arrow::BooleanScalar>(document.GetBool());
    case TypeID::kInt:
      Ensure(document.IsInt(), std::string(__PRETTY_FUNCTION__) + ": Expected Int value");
      return std::make_shared<arrow::Int32Scalar>(document.GetInt());
    case TypeID::kLong:
      Ensure(document.IsInt64(), std::string(__PRETTY_FUNCTION__) + ": Expected Int64 value");
      return std::make_shared<arrow::Int64Scalar>(document.GetInt64());
    case TypeID::kFloat:
      Ensure(document.IsFloat(), std::string(__PRETTY_FUNCTION__) + ": Expected Float value");
      return std::make_shared<arrow::FloatScalar>(document.GetFloat());
    case TypeID::kDouble:
      Ensure(document.IsDouble(), std::string(__PRETTY_FUNCTION__) + ": Expected Double value");
      return std::make_shared<arrow::DoubleScalar>(document.GetDouble());
    case TypeID::kDecimal: {
      Ensure(document.IsString(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");
      auto decimal = std::static_pointer_cast<const types::DecimalType>(type);
      auto precision = decimal->Precision();
      auto scale = decimal->Scale();
      std::string str = document.GetString();
      if (scale < 0) {
        auto it = str.find("E+");
        Ensure(it != std::string::npos,
               std::string(__PRETTY_FUNCTION__) + ": Negative scale values must be presented in scientific notation");
        auto str_scale = std::stoi(str.substr(it + 2));
        Ensure(str_scale == -scale, ": The exponent must equal the negated scale");
        str = str.substr(0, it) + std::string('0', str_scale);
        precision += str_scale;
        scale = 0;
      }
      Ensure(precision <= 76, std::string(__PRETTY_FUNCTION__) + ": Decimal precision greater than 76 is unsupported");
      if (precision > 38) {
        auto decimal_value = arrow::Decimal256::FromString(str).ValueOrDie();
        auto type = std::make_shared<arrow::Decimal256Type>(precision, scale);
        return std::make_shared<arrow::Decimal256Scalar>(decimal_value, type);
      }
      auto decimal_value = arrow::Decimal128::FromString(str).ValueOrDie();
      auto type = std::make_shared<arrow::Decimal128Type>(precision, scale);
      return std::make_shared<arrow::Decimal128Scalar>(decimal_value, type);
    }
    case TypeID::kDate: {
      int64_t sec_since_epoch = ParseTime(document, arrow::TimeUnit::SECOND);
      return std::make_shared<arrow::Date32Scalar>(sec_since_epoch / 86400);
    }
    case TypeID::kTime: {
      Ensure(document.IsString(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");
      int64_t micros_since_epoch;
      std::string str = "1970-01-01T" + std::string(document.GetString());
      auto parser = arrow::TimestampParser::MakeISO8601();
      Ensure(parser->operator()(str.data(), str.size(), arrow::TimeUnit::MICRO, &micros_since_epoch),
             std::string(__PRETTY_FUNCTION__) + ": Failed to parse Time");
      return std::make_shared<arrow::Time64Scalar>(micros_since_epoch, arrow::TimeUnit::MICRO);
    }
    case TypeID::kTimestamp: {
      int64_t micros_since_epoch = ParseTime(document, arrow::TimeUnit::MICRO);
      return std::make_shared<arrow::TimestampScalar>(micros_since_epoch, arrow::TimeUnit::MICRO);
    }
    case TypeID::kTimestamptz: {
      int64_t micros_since_epoch = ParseTime(document, arrow::TimeUnit::MICRO);
      return std::make_shared<arrow::TimestampScalar>(micros_since_epoch, arrow::TimeUnit::MICRO, "UTC");
    }
    case TypeID::kTimestampNs: {
      int64_t nanos_since_epoch = ParseTime(document, arrow::TimeUnit::NANO);
      return std::make_shared<arrow::TimestampScalar>(nanos_since_epoch, arrow::TimeUnit::NANO);
    }
    case TypeID::kTimestamptzNs: {
      int64_t nanos_since_epoch = ParseTime(document, arrow::TimeUnit::NANO);
      return std::make_shared<arrow::TimestampScalar>(nanos_since_epoch, arrow::TimeUnit::NANO, "UTC");
    }
    case TypeID::kString:
      Ensure(document.IsString(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");
      return std::make_shared<arrow::StringScalar>(document.GetString());
    case TypeID::kUuid: {
      Ensure(document.IsString(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");

      std::string clean_uuid = document.GetString();
      clean_uuid.erase(std::remove(clean_uuid.begin(), clean_uuid.end(), '-'), clean_uuid.end());

      const size_t uuid_length = 16;

      Ensure(clean_uuid.size() == 2 * uuid_length, std::string(__PRETTY_FUNCTION__) +
                                                       ": Expected UUID string length of " +
                                                       std::to_string(2 * uuid_length) + " characters");

      auto buffer = arrow::AllocateBuffer(uuid_length).ValueOrDie();
      uint8_t* data = buffer->mutable_data();

      for (int i = 0; i < uuid_length; ++i) {
        std::string byte_str = clean_uuid.substr(i * 2, 2);
        data[i] = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
      }

      return std::make_shared<arrow::FixedSizeBinaryScalar>(std::shared_ptr<arrow::Buffer>(buffer.release()),
                                                            arrow::fixed_size_binary(uuid_length));
    }
    case TypeID::kFixed: {
      Ensure(document.IsString(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");
      size_t size = std::static_pointer_cast<const types::FixedType>(type)->Size();
      Ensure(size == document.GetStringLength(),
             std::string(__PRETTY_FUNCTION__) + ": Expected String length of " + std::to_string(size));

      auto buffer = std::make_shared<arrow::Buffer>(std::string_view(document.GetString()));
      return std::make_shared<arrow::FixedSizeBinaryScalar>(buffer, arrow::fixed_size_binary(size));
    }
    case TypeID::kBinary: {
      Ensure(document.IsString(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");

      auto buffer = std::make_shared<arrow::Buffer>(std::string_view(document.GetString()));
      return std::make_shared<arrow::FixedSizeBinaryScalar>(buffer, arrow::binary());
    }
    case TypeID::kList: {
      Ensure(document.IsArray(), std::string(__PRETTY_FUNCTION__) + ": Expected String value");
      auto element_type = std::static_pointer_cast<const types::ListType>(type)->ElementType();
      int size = document.Size();
      std::vector<std::shared_ptr<arrow::Scalar>> scalars(size);
      for (int i = 0; i < size; ++i) {
        scalars[i] = GetScalar(element_type, document[i]);
      }
      std::unique_ptr<arrow::ArrayBuilder> builder;
      Ensure(arrow::MakeBuilder(arrow::default_memory_pool(), scalars[0]->type, &builder) == arrow::Status::OK(),
             std::string(__PRETTY_FUNCTION__) + ": Failed to create ArrayBuilder");
      Ensure(builder->AppendScalars(scalars) == arrow::Status::OK(),
             std::string(__PRETTY_FUNCTION__) + ": Failed to add scalars to ArrayBuilder");
      std::shared_ptr<arrow::Array> out;
      Ensure(builder->Finish(&out) == arrow::Status::OK(),
             std::string(__PRETTY_FUNCTION__) + ": Failed to get an Array object from ArrayBuilder");
      return std::make_shared<arrow::ListScalar>(out);
    }
    default:
      Ensure(false, std::string(__PRETTY_FUNCTION__) + ": Unsupported type");
  }
}

}  // namespace

std::shared_ptr<arrow::Array> Literal::MakeColumn(int64_t length) {
  return MakeArrayFromScalar(*scalar_, length).ValueOrDie();
}

Literal ParseLiteral(std::shared_ptr<const types::Type> type, const rapidjson::Value& document) {
  return Literal(GetScalar(type, document));
}

}  // namespace iceberg
