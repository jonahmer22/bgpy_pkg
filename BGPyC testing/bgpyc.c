#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef struct {
    PyObject_HEAD
    unsigned int asn;
    unsigned long counter;
} ASObject;

static PyObject *AS_step(ASObject *self, PyObject *Py_UNUSED(ignored)) {
    self->counter += 1;
    return PyLong_FromUnsignedLong(self->counter);
}

static int AS_init(ASObject *self, PyObject *args, PyObject *kwds) {
    static char *kwlist[] = {"asn", NULL};
    unsigned int asn = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "I", kwlist, &asn)) {
        return -1;
    }
    self->asn = asn;
    self->counter = 0;
    return 0;
}

static PyObject *AS_repr(ASObject *self) {
    return PyUnicode_FromFormat("<bgpyc.AS asn=%u counter=%lu>", self->asn, self->counter);
}

/* Methods on the type */
static PyMethodDef AS_methods[] = {
    {"step", (PyCFunction)AS_step, METH_NOARGS, "Advance one simulation step (C fast-path)."},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject ASType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "bgpyc.AS",
    .tp_basicsize = sizeof(ASObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* IMPORTANT: allows subclassing */
    .tp_doc = "C-defined AS object (subclassable).",
    .tp_methods = AS_methods,
    .tp_init = (initproc)AS_init,
    .tp_new = PyType_GenericNew,
    .tp_repr = (reprfunc)AS_repr,
};

/*
 * Hybrid dispatch hook:
 * - exact bgpyc.AS => call the C implementation directly (fast)
 * - subclass/other => call obj.step() (allows Python override)
 */
static PyObject *call_step(PyObject *Py_UNUSED(self), PyObject *obj) {
    if (PyObject_TypeCheck(obj, &ASType) && Py_TYPE(obj) == &ASType) {
        return AS_step((ASObject *)obj, NULL);
    }
    return PyObject_CallMethod(obj, "step", NULL);
}

static PyMethodDef module_methods[] = {
    {"call_step", (PyCFunction)call_step, METH_O, "Call obj.step() with a fast-path for exact bgpyc.AS."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef bgpyc_module = {
    PyModuleDef_HEAD_INIT,
    "bgpyc",
    "BGPyc proof-of-concept module.",
    -1,
    module_methods
};

PyMODINIT_FUNC PyInit_bgpyc(void) {
    if (PyType_Ready(&ASType) < 0) return NULL;

    PyObject *m = PyModule_Create(&bgpyc_module);
    if (!m) return NULL;

    Py_INCREF(&ASType);
    if (PyModule_AddObject(m, "AS", (PyObject *)&ASType) < 0) {
        Py_DECREF(&ASType);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}