#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeFixedString.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnFixedString.h>
#include <Columns/ColumnArray.h>
#include <Interpreters/Context_fwd.h>
#include <Interpreters/ITokenExtractor.h>
#include <Functions/IFunction.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/FunctionFactory.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
}

class FunctionNgrams : public IFunction
{
public:

    static constexpr auto name = "ngrams";

    static FunctionPtr create(ContextPtr)
    {
        return std::make_shared<FunctionNgrams>();
    }

    String getName() const override { return name; }

    size_t getNumberOfArguments() const override { return 2; }
    bool isVariadic() const override { return false; }
    ColumnNumbers getArgumentsThatAreAlwaysConstant() const override { return {1}; }

    bool useDefaultImplementationForNulls() const override { return true; }
    bool useDefaultImplementationForConstants() const override { return true; }
    bool useDefaultImplementationForLowCardinalityColumns() const override { return true; }
    bool isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const override { return true; }

    DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName & arguments) const override
    {
        auto ngram_input_argument_type = WhichDataType(arguments[0].type);
        if (!ngram_input_argument_type.isStringOrFixedString())
            throw Exception(ErrorCodes::BAD_ARGUMENTS,
                "Function {} second argument type should be String or FixedString. Actual {}",
                getName(),
                arguments[0].type->getName());

        const auto & column_with_type = arguments[1];
        const auto & ngram_argument_column = arguments[1].column;
        auto ngram_argument_type = WhichDataType(column_with_type.type);

        if (!ngram_argument_type.isNativeUInt() || !ngram_argument_column || !isColumnConst(*ngram_argument_column))
            throw Exception(ErrorCodes::BAD_ARGUMENTS,
                "Function {} second argument type should be constant UInt. Actual {}",
                getName(),
                arguments[1].type->getName());

        Field ngram_argument_value;
        ngram_argument_column->get(0, ngram_argument_value);
        auto ngram_value = ngram_argument_value.safeGet<UInt64>();

        return std::make_shared<DataTypeArray>(std::make_shared<DataTypeFixedString>(ngram_value));
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t) const override
    {
        Field ngram_argument_value;
        arguments[1].column->get(0, ngram_argument_value);
        auto ngram_value = ngram_argument_value.safeGet<UInt64>();

        NgramTokenExtractor extractor(ngram_value);

        auto result_column_fixed_string = ColumnFixedString::create(ngram_value);
        auto column_offsets = ColumnArray::ColumnOffsets::create();

        auto input_column = arguments[0].column;
        if (const auto * column_string = checkAndGetColumn<ColumnString>(input_column.get()))
            executeImpl(extractor, *column_string, *result_column_fixed_string, *column_offsets);
        else if (const auto * column_fixed_string = checkAndGetColumn<ColumnFixedString>(input_column.get()))
            executeImpl(extractor, *column_fixed_string, *result_column_fixed_string, *column_offsets);

        return ColumnArray::create(std::move(result_column_fixed_string), std::move(column_offsets));
    }

private:

    template <typename StringColumnType>
    inline void executeImpl(const NgramTokenExtractor & extractor, StringColumnType & input_data_column, ColumnFixedString & result_data_column, ColumnArray::ColumnOffsets & offsets_column) const
    {
        size_t current_tokens_size = 0;
        auto & offsets_data = offsets_column.getData();

        size_t column_size = input_data_column.size();
        offsets_data.resize(column_size);

        for (size_t i = 0; i < column_size; ++i)
        {
            auto data = input_data_column.getDataAt(i);

            size_t cur = 0;
            size_t token_start = 0;
            size_t token_length = 0;

            while (cur < data.size && extractor.nextInString(data.data, data.size, &cur, &token_start, &token_length))
            {
                result_data_column.insertData(data.data + token_start, token_length);
                ++current_tokens_size;
            }

            offsets_data[i] = current_tokens_size;
        }
    }
};

void registerFunctionNgrams(FunctionFactory & factory)
{
    factory.registerFunction<FunctionNgrams>();
}

}


