"""CUBRID FIELD_TYPE Constants

These constants represent the various column (field) types that are
supported by CUBRID.

"""
#CUBRID의 필드 종류 컨텐츠
#CURIRD에서 지원되는 다양한 필드타입을 

CHAR    = 1
VARCHAR = 2
NCHAR   = 3
VARNCHAR = 4

BIT     = 5
VARBIT  = 6

NUMERIC = 7
INT     = 8
SMALLINT = 9
MONETARY = 10
BIGINT = 21

FLOAT   = 11
DOUBLE  = 12

DATE    = 13
TIME    = 14
TIMESTAMP   = 15
OBJECT = 19

SET     = 32
MULTISET    = 64 
SEQUENCE    = 96 

BLOB        = 254
CLOB        = 255

STRING = VARCHAR
