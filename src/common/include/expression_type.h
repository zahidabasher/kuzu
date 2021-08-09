#pragma once

#include <cstdint>
#include <string>

using namespace std;

namespace graphflow {
namespace common {

/**
 * Function name is a temporary identifier used for binder because grammar does not parse built in
 * functions. After binding, expression type should replace function name and used as identifier.
 */
const string ABS_FUNC_NAME = "ABS";
const string COUNT_STAR_FUNC_NAME = "COUNT_STAR";
const string ID_FUNC_NAME = "ID";
const string DATE_FUNC_NAME = "DATE";

enum ExpressionType : uint8_t {

    /**
     * Boolean Connection Expressions
     **/
    OR = 0,
    XOR = 1,
    AND = 2,
    NOT = 3,

    /**
     * Comparison Expressions
     **/
    EQUALS = 10,
    NOT_EQUALS = 11,
    GREATER_THAN = 12,
    GREATER_THAN_EQUALS = 13,
    LESS_THAN = 14,
    LESS_THAN_EQUALS = 15,

    /**
     * Arithmetic Expressions
     **/
    ADD = 20,
    SUBTRACT = 21,
    MULTIPLY = 22,
    DIVIDE = 23,
    MODULO = 24,
    POWER = 25,
    NEGATE = 26,

    /**
     * NODE ID Expressions
     * */
    HASH_NODE_ID = 32,
    EQUALS_NODE_ID = 33,
    NOT_EQUALS_NODE_ID = 34,
    GREATER_THAN_NODE_ID = 35,
    GREATER_THAN_EQUALS_NODE_ID = 36,
    LESS_THAN_NODE_ID = 37,
    LESS_THAN_EQUALS_NODE_ID = 38,

    /**
     * String Operator Expressions
     **/
    STARTS_WITH = 40,
    ENDS_WITH = 41,
    CONTAINS = 42,

    /**
     * List Operator Expressions works only for CSV Line
     */
    CSV_LINE_EXTRACT = 45,

    /**
     * Null Operator Expressions
     **/
    IS_NULL = 50,
    IS_NOT_NULL = 51,

    /**
     * Property Expression
     **/
    PROPERTY = 60,

    /**
     * Literal Expressions
     **/
    LITERAL_INT = 80,
    LITERAL_DOUBLE = 81,
    LITERAL_STRING = 82,
    LITERAL_BOOLEAN = 83,
    LITERAL_DATE = 84,
    LITERAL_NULL = 85,

    /**
     * Variable Expression
     **/
    VARIABLE = 90,
    ALIAS = 91,

    /**
     * Cast Expressions
     **/
    CAST_TO_STRING = 100,
    CAST_TO_UNSTRUCTURED_VECTOR = 101,
    CAST_UNSTRUCTURED_VECTOR_TO_BOOL_VECTOR = 102,

    FUNCTION = 110,

    /**
     * Arithmetic Function Expression
     **/
    ABS_FUNC = 111,

    /**
     * Aggregation Function Expression
     */
    COUNT_STAR_FUNC = 130,

    /**
     * Scalar Function Expression
     */
    ID_FUNC = 150,

    /**
     * Temporal Function Expression
     */
    DATE_FUNC = 170,

    EXISTENTIAL_SUBQUERY = 190,
};

bool isExpressionUnary(ExpressionType type);
bool isExpressionBinary(ExpressionType type);
bool isExpressionBoolConnection(ExpressionType type);
bool isExpressionComparison(ExpressionType type);
bool isExpressionIDComparison(ExpressionType type);
bool isExpressionArithmetic(ExpressionType type);
bool isExpressionStringOperator(ExpressionType type);
bool isExpressionNullComparison(ExpressionType type);
bool isExpressionLiteral(ExpressionType type);

ExpressionType comparisonToIDComparison(ExpressionType type);
string expressionTypeToString(ExpressionType type);

} // namespace common
} // namespace graphflow
