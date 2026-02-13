// proof of concept
// only for validating basic functions (inheretance, runtime, function call time)
// use as a blueprint for actual implementations (start with Graph nodes)
#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef struct {
    PyObject_HEAD
    unsigned int asn;
    unsigned long counter;
} ASObject;

// forgot this
static PyTypeObject ASType;

static PyObject *AS_step(ASObject *self, PyObject *Py_UNUSED(ignored)) {
    self->counter += 1;
    return PyLong_FromUnsignedLong(self->counter);
}

static PyObject *AS_step_n_raw(ASObject *self, unsigned long n) {
    self->counter += n;
    return PyLong_FromUnsignedLong(self->counter);
}

static PyObject *AS_step_n(ASObject *self, PyObject *args) {
    unsigned long n = 0;
    if (!PyArg_ParseTuple(args, "k", &n)) {
        return NULL;
    }
    return AS_step_n_raw(self, n);
}

static PyObject *AS_method_a(ASObject *Py_UNUSED(self), PyObject *Py_UNUSED(ignored)) {
    PySys_WriteStdout("in method A\n");
    Py_RETURN_NONE;
}

static PyObject *AS_method_b(ASObject *Py_UNUSED(self), PyObject *args) {
    const char *msg = NULL;
    if (!PyArg_ParseTuple(args, "s", &msg)) {
        return NULL;
    }
    PySys_WriteStdout("in method B: %s\n", msg);
    Py_RETURN_NONE;
}

static PyObject *AS_method_c(ASObject *Py_UNUSED(self), PyObject *Py_UNUSED(ignored)) {
    PySys_WriteStdout("in method C\n");
    Py_RETURN_NONE;
}

static PyObject *AS_bump(ASObject *self, PyObject *args) {
    unsigned long delta = 0;
    if (!PyArg_ParseTuple(args, "k", &delta)) {
        return NULL;
    }
    self->counter += delta;
    return PyLong_FromUnsignedLong(self->counter);
}

static PyObject *AS_reset(ASObject *self, PyObject *Py_UNUSED(ignored)) {
    self->counter = 0;
    Py_RETURN_NONE;
}

static PyObject *AS_get_asn(ASObject *self, PyObject *Py_UNUSED(ignored)) {
    return PyLong_FromUnsignedLong(self->asn);
}

static PyObject *AS_get_counter(ASObject *self, PyObject *Py_UNUSED(ignored)) {
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

// caches whether methods are overriden.
// NOTE: breaks monkey patching (personally don't like it anyway, but it breaks it)
static int method_is_overridden_cached(PyObject *obj, const char *name, const char *cache_key) {
    PyObject *type = (PyObject *)Py_TYPE(obj);

    // Exact type: never overridden
    if (type == (PyObject *)&ASType) {
        return 0;
    }

    // a object holds whether it was cached or not as an attribute
    PyObject *cached = PyObject_GetAttrString(type, cache_key);
    if (cached) {
        int overridden = PyObject_IsTrue(cached);
        Py_DECREF(cached);
        return overridden;
    }
    PyErr_Clear();  // if there wasn't one it errors, but that's not really an error

    PyObject *type_attr = PyObject_GetAttrString(type, name);
    if (!type_attr) {
        PyErr_Clear();
        return 1;
    }
    PyObject *base_attr = PyObject_GetAttrString((PyObject *)&ASType, name);
    if (!base_attr) {
        PyErr_Clear();
        Py_DECREF(type_attr);
        return 1;
    }
    int overridden = (type_attr != base_attr);
    Py_DECREF(type_attr);
    Py_DECREF(base_attr);

    PyObject *flag = overridden ? Py_True : Py_False;
    Py_INCREF(flag);
    if (PyObject_SetAttrString(type, cache_key, flag) < 0) {
        PyErr_Clear();
    }
    Py_DECREF(flag);

    return overridden;
}

/* Methods on the type */
static PyMethodDef AS_methods[] = {
    {"step", (PyCFunction)AS_step, METH_NOARGS, "Advance one simulation step (C fast-path)."},
    {"step_n", (PyCFunction)AS_step_n, METH_VARARGS, "Advance n simulation steps (C loop)."},
    {"method_a", (PyCFunction)AS_method_a, METH_NOARGS, "Print a test message (method A)."},
    {"method_b", (PyCFunction)AS_method_b, METH_VARARGS, "Print a test message (method B)."},
    {"method_c", (PyCFunction)AS_method_c, METH_NOARGS, "Print a test message (method C)."},
    {"bump", (PyCFunction)AS_bump, METH_VARARGS, "Increment counter by a delta."},
    {"reset", (PyCFunction)AS_reset, METH_NOARGS, "Reset counter to zero."},
    {"get_asn", (PyCFunction)AS_get_asn, METH_NOARGS, "Return the ASN."},
    {"get_counter", (PyCFunction)AS_get_counter, METH_NOARGS, "Return the counter."},
    {NULL, NULL, 0, NULL}   // used as an end of list item in python
};

static PyTypeObject ASType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "bgpyc.AS",
    .tp_basicsize = sizeof(ASObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   // IMPORTANT: allows subclassing
    .tp_doc = "C-defined AS object (subclassable).",
    .tp_methods = AS_methods,
    .tp_init = (initproc)AS_init,
    .tp_new = PyType_GenericNew,
    .tp_repr = (reprfunc)AS_repr,
};

/*
 * Hybrid dispatch hook:
 * - exact bgpyc.AS => call the C implementation directly (fast)
 * - subclass => call C if not overridden, else obj.step()
 * - other => call obj.step()
 */
static PyObject *call_step(PyObject *Py_UNUSED(self), PyObject *obj) {
    if (Py_TYPE(obj) == &ASType) {
        return AS_step((ASObject *)obj, NULL);
    }
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "step", "__bgpyc_override_step")) {
        return AS_step((ASObject *)obj, NULL);
    }
    return PyObject_CallMethod(obj, "step", NULL);
}

static PyObject *call_step_n(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *obj = NULL;
    unsigned long n = 0;
    if (!PyArg_ParseTuple(args, "Ok", &obj, &n)) {
        return NULL;
    }
    if (Py_TYPE(obj) == &ASType) {
        return AS_step_n_raw((ASObject *)obj, n);
    }
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "step_n", "__bgpyc_override_step_n")) {
        return AS_step_n_raw((ASObject *)obj, n);
    }
    return PyObject_CallMethod(obj, "step_n", "k", n);
}

static PyObject *call_method_a(PyObject *Py_UNUSED(self), PyObject *obj) {
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "method_a", "__bgpyc_override_method_a")) {
        return AS_method_a((ASObject *)obj, NULL);
    }
    return PyObject_CallMethod(obj, "method_a", NULL);
}

static PyObject *call_method_b(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *obj = NULL;
    const char *msg = NULL;
    if (!PyArg_ParseTuple(args, "Os", &obj, &msg)) {
        return NULL;
    }
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "method_b", "__bgpyc_override_method_b")) {
        PyObject *method_args = Py_BuildValue("(s)", msg);
        if (!method_args) return NULL;
        PyObject *result = AS_method_b((ASObject *)obj, method_args);
        Py_DECREF(method_args);
        return result;
    }
    return PyObject_CallMethod(obj, "method_b", "s", msg);
}

static PyObject *call_method_c(PyObject *Py_UNUSED(self), PyObject *obj) {
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "method_c", "__bgpyc_override_method_c")) {
        return AS_method_c((ASObject *)obj, NULL);
    }
    return PyObject_CallMethod(obj, "method_c", NULL);
}

static PyObject *call_bump(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *obj = NULL;
    unsigned long delta = 0;
    if (!PyArg_ParseTuple(args, "Ok", &obj, &delta)) {
        return NULL;
    }
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "bump", "__bgpyc_override_bump")) {
        PyObject *method_args = Py_BuildValue("(k)", delta);
        if (!method_args) return NULL;
        PyObject *result = AS_bump((ASObject *)obj, method_args);
        Py_DECREF(method_args);
        return result;
    }
    return PyObject_CallMethod(obj, "bump", "k", delta);
}

static PyObject *call_reset(PyObject *Py_UNUSED(self), PyObject *obj) {
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "reset", "__bgpyc_override_reset")) {
        return AS_reset((ASObject *)obj, NULL);
    }
    return PyObject_CallMethod(obj, "reset", NULL);
}

static PyObject *call_get_asn(PyObject *Py_UNUSED(self), PyObject *obj) {
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "get_asn", "__bgpyc_override_get_asn")) {
        return AS_get_asn((ASObject *)obj, NULL);
    }
    return PyObject_CallMethod(obj, "get_asn", NULL);
}

static PyObject *call_get_counter(PyObject *Py_UNUSED(self), PyObject *obj) {
    if (PyObject_TypeCheck(obj, &ASType) && !method_is_overridden_cached(obj, "get_counter", "__bgpyc_override_get_counter")) {
        return AS_get_counter((ASObject *)obj, NULL);
    }
    return PyObject_CallMethod(obj, "get_counter", NULL);
}

static PyMethodDef module_methods[] = {
    {"call_step", (PyCFunction)call_step, METH_O, "Call obj.step() with a fast-path for exact bgpyc.AS."},
    {"call_step_n", (PyCFunction)call_step_n, METH_VARARGS, "Call obj.step_n(n) with a fast-path when not overridden."},
    {"call_method_a", (PyCFunction)call_method_a, METH_O, "Call obj.method_a() with a fast-path when not overridden."},
    {"call_method_b", (PyCFunction)call_method_b, METH_VARARGS, "Call obj.method_b(msg) with a fast-path when not overridden."},
    {"call_method_c", (PyCFunction)call_method_c, METH_O, "Call obj.method_c() with a fast-path when not overridden."},
    {"call_bump", (PyCFunction)call_bump, METH_VARARGS, "Call obj.bump(delta) with a fast-path when not overridden."},
    {"call_reset", (PyCFunction)call_reset, METH_O, "Call obj.reset() with a fast-path when not overridden."},
    {"call_get_asn", (PyCFunction)call_get_asn, METH_O, "Call obj.get_asn() with a fast-path when not overridden."},
    {"call_get_counter", (PyCFunction)call_get_counter, METH_O, "Call obj.get_counter() with a fast-path when not overridden."},
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
