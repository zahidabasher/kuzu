#include "include/py_connection.h"
#include "include/py_database.h"
#include "include/py_loader.h"

void bind(py::module& m) {
    PyLoader::initialize(m);
    PyDatabase::initialize(m);
    PyConnection::initialize(m);
    PyQueryResult::initialize(m);

    m.doc() = "GrpahflowDB is an embedded graph database";
}

PYBIND11_MODULE(_graphflowdb, m) {
    bind(m);
}
