#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

/*
 * C Announcement implementation.
 *
 * This mirrors bgpy.simulation_engine.announcement.Announcement and keeps the
 * same public API and runtime validation semantics while removing dataclasses
 * replace()/copy overhead from the hot path.
 */

// ##############################
// # Announcement Functionality #
// ##############################

/*
 * Announcement object
 * 
 * holds all values that the Python object must have and are needed for C computation 
 */
typedef struct {
    /* Python Compatability Macro */
    PyObject_HEAD

    /* Announcement attributes */
    PyObject *prefix;
    PyObject *as_path;
    // Equivalent to the next hop in a normal BGP announcement
    PyObject *next_hop_asn; // type: ignore
    PyObject *seed_asn;
    PyObject *recv_relationship;

    /* Optional attributes below (kept as object references for parity) */
    /*
     * If you aren't using the policies listed below,
     * You can create an announcement class without them
     * for a much faster runtime. Announcement copying is
     * the bottleneck for BGPy, smaller announcements copy
     * much faster across the AS topology
    */

    /*
     * This currently is unused. Depending on some results from
     * our in-progress publications it may be used in the future
     * For now, we just set the timestamp of the victim to 0,
     * and timestamp of the attacker to 1
    */
    PyObject *timestamp;
    // Used for classes derived from BGPFull
    PyObject *withdraw;
    // BGPsec optional attributes
    // BGPsec next ASN that should receive the control plane announcement
    // NOTE: this is the opposite direction of next_hop, for the data plane
    PyObject *bgpsec_next_asn;
    PyObject *bgpsec_as_path;
    // RFC 9234 OTC attribute (Used in OnlyToCustomers Policy)
    PyObject *only_to_customers;
    // ROV++ attribute
    PyObject *rovpp_blackhole;

    /* Hash is expensive to recompute; cache after first calculation. */
    Py_hash_t hash_cache;
    int hash_computed;
} AnnouncementObject;

static PyTypeObject AnnouncementType;
static PyObject *relationships_origin = NULL;
static PyObject *empty_tuple_singleton = NULL;

/* Sets all attribures to _Py_NULL and clears hash */
static void Announcement_clear_fields(AnnouncementObject *self) {
    /* these can maybe be simplified, technically the Py_CLEAR macro is in C but it isn't all to efficient */
    Py_CLEAR(self->prefix);
    Py_CLEAR(self->as_path);
    Py_CLEAR(self->next_hop_asn);
    Py_CLEAR(self->seed_asn);
    Py_CLEAR(self->recv_relationship);
    Py_CLEAR(self->timestamp);
    Py_CLEAR(self->withdraw);
    Py_CLEAR(self->bgpsec_next_asn);
    Py_CLEAR(self->bgpsec_as_path);
    Py_CLEAR(self->only_to_customers);
    Py_CLEAR(self->rovpp_blackhole);
    self->hash_computed = 0;
}

/* Assigns values to all fields and attributes of an announcement */
static int Announcement_assign_fields(
    AnnouncementObject *self,
    PyObject *prefix,
    PyObject *as_path,
    PyObject *next_hop_asn,
    PyObject *seed_asn,
    PyObject *recv_relationship,
    PyObject *timestamp,
    PyObject *withdraw,
    PyObject *bgpsec_next_asn,
    PyObject *bgpsec_as_path,
    PyObject *only_to_customers,
    PyObject *rovpp_blackhole
) {
    /* 
     * increase references to attributes
     * prevents GC from touching them while having values (we are about to set)
     */
    Py_INCREF(prefix);
    Py_INCREF(as_path);
    Py_INCREF(next_hop_asn);
    Py_INCREF(seed_asn);
    Py_INCREF(recv_relationship);
    Py_INCREF(timestamp);
    Py_INCREF(withdraw);
    Py_INCREF(bgpsec_next_asn);
    Py_INCREF(bgpsec_as_path);
    Py_INCREF(only_to_customers);
    Py_INCREF(rovpp_blackhole);

    // remove any values already in attributes
    Announcement_clear_fields(self);

    // set values from args
    self->prefix = prefix;
    self->as_path = as_path;
    self->next_hop_asn = next_hop_asn;
    self->seed_asn = seed_asn;
    self->recv_relationship = recv_relationship;
    self->timestamp = timestamp;
    self->withdraw = withdraw;
    self->bgpsec_next_asn = bgpsec_next_asn;
    self->bgpsec_as_path = bgpsec_as_path;
    self->only_to_customers = only_to_customers;
    self->rovpp_blackhole = rovpp_blackhole;
    self->hash_computed = 0;

    return 0;
}

/* Clears all attributes and frees an announcement */
static void Announcement_dealloc(AnnouncementObject *self) {
    Announcement_clear_fields(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/*
 * Frozen dataclass parity: all assignment after __init__ is rejected.
 * (no monkeypatching allowed with C extensions and caching)
 */
static int Announcement_setattro(
    PyObject *Py_UNUSED(obj),
    PyObject *name,
    PyObject *Py_UNUSED(value)
) {
    // create an error that describes the attribute (field) that was attempted to be written to
    PyErr_Format(
        PyExc_AttributeError,
        "cannot assign to field %R",
        name
    );
    return -1;  // python will then catch this error
}

/*  
 * This function is equivilent to `__init__`
 * Note: not subclassing from YamlAble, that happens in the python portion
 * 
 * Contains all initialization of datatypes and parses arguements for creation
*/
static int Announcement_init(AnnouncementObject *self, PyObject *args, PyObject *kwds) {
    // ========================================
    // Set Up pre-reqs and Initialize Variables
    // ========================================

    // null teriminated list of attributes
    static char *kwlist[] = {
        "prefix",
        "as_path",
        "next_hop_asn",
        "seed_asn",
        "recv_relationship",
        "timestamp",
        "withdraw",
        "bgpsec_next_asn",
        "bgpsec_as_path",
        "only_to_customers",
        "rovpp_blackhole",
        NULL
    };

    // initialize all values as NULL or Py_None values
    PyObject *prefix = NULL;
    PyObject *as_path = NULL;
    PyObject *next_hop_asn = Py_None;
    PyObject *seed_asn = Py_None;
    PyObject *recv_relationship = NULL;
    PyObject *timestamp = NULL;
    PyObject *withdraw = NULL;
    PyObject *bgpsec_next_asn = Py_None;
    PyObject *bgpsec_as_path = NULL;
    PyObject *only_to_customers = Py_None;
    PyObject *rovpp_blackhole = NULL;

    // parse the values from arguements into PyObjects we created
    if (!PyArg_ParseTupleAndKeywords(
            args,                   // args from python function call
            kwds,                   // kwds from python function call
            "OO|OOOOOOOOO",         // first 2 arguements are required all others are optional
            kwlist,                 // list of attributes we want to parse **in order** (fetched by name)
            // addresses of where we want the values to go **in order**
            &prefix,
            &as_path,
            &next_hop_asn,
            &seed_asn,
            &recv_relationship,
            &timestamp,
            &withdraw,
            &bgpsec_next_asn,
            &bgpsec_as_path,
            &only_to_customers,
            &rovpp_blackhole)) {
        return -1;  // if that fails for some reason error out
    }

    // ==========================================
    // Make Sure Everything is Assigned Correctly
    // ==========================================

    // ensure the type of prefix is a python string
    if (!PyUnicode_Check(prefix)) {
        // if not set error message and return -1
        PyErr_SetString(PyExc_TypeError, "prefix must be a str");
        return -1;
    }
    // ensure the type of as_path is a python tuple
    if (!PyTuple_Check(as_path)) {
        // if not set error message and return -1
        PyErr_SetString(PyExc_TypeError, "as_path must be a tuple");
        return -1;
    }

    // if recv_relationship is not set then set it to the relationships_origin PyObject (which is initialized as null)
    if (!recv_relationship) {
        recv_relationship = relationships_origin;
    }
    // if timestamp is not set then initialize it as a python integer 0
    if (!timestamp) {
        timestamp = PyLong_FromLong(0); // don't need to INCREF (function does it already)
        // if that somehow fails error out
        if (!timestamp) {
            return -1;
        }
    } else {
        // if the timestamp was set increase reference count so the GC doesn't touch it
        Py_INCREF(timestamp);
    }
    // if withdraw is not set initialize it to a Py_False
    if (!withdraw) {
        withdraw = Py_False;
        Py_INCREF(withdraw);    
    } else {    // either way (above and below) increase reference so GC doesn't touch
        Py_INCREF(withdraw);
    }
    // if the bpgsec_as_path was not given set it to an empty_tuple_singleton
    if (!bgpsec_as_path) {
        bgpsec_as_path = empty_tuple_singleton; // empty_tuple_singleton is initialized as NULL above
        Py_INCREF(bgpsec_as_path);
    } else {    // either way (above and below) increase reference so GC doesn't touch
        Py_INCREF(bgpsec_as_path);
    }
    // if rovpp_backhole was not provided set it to Py_False and increase reference
    if (!rovpp_blackhole) {
        rovpp_blackhole = Py_False;
        Py_INCREF(rovpp_blackhole);
    } else {    // either way (above and below) increase reference so GC doesn't touch
        Py_INCREF(rovpp_blackhole);
    }
    // increase reference to recv_relationship, no checking needed, but need to mark as using for GC
    Py_INCREF(recv_relationship);

    // =======================================
    // seed_asn and next_hop_asn None checking 
    // =======================================

    /* 
     * Since this gets called with replace where seed_asn None is valid,
     * can't do any other checks. Even this should probably be moved out due
     * to unnecessary overhead 
     * 
     * (preserved from Python implementation comments).
     * 
     * Note: the code below does the same thing as it did in python idk if it should still be moved
     */
    Py_ssize_t path_len = PyTuple_GET_SIZE(as_path);
    if (path_len == 1 && seed_asn == Py_None) {
        seed_asn = PyTuple_GET_ITEM(as_path, 0);
    }

    /* next hop defaults to None, messing up the type (comment from python) */
    if (next_hop_asn == Py_None) {
        if (path_len == 1) {    // type: ignore
            next_hop_asn = PyTuple_GET_ITEM(as_path, 0);
        } else if (path_len > 1) {
            /* raise a ValueError */

            // create an identical error message as to what was in python
            PyObject *msg = PyUnicode_FromFormat(
                "Announcement was initialized with an AS path longer than 1 (%R) "
                "but the next_hop_asn is ambiguous.  next_hop_asn is where the "
                "traffic should route to next.Please add the next_hop_asn to the "
                "initialization parameters for the announcement of prefix %U",
                as_path,
                prefix
            );

            // if for some reason the message wasn't properly created just decref everything and exit
            if (!msg) {
                Py_DECREF(recv_relationship);
                Py_DECREF(timestamp);
                Py_DECREF(withdraw);
                Py_DECREF(bgpsec_as_path);
                Py_DECREF(rovpp_blackhole);
                return -1;
            }
            // set the type of msg as a ValueError
            PyErr_SetObject(PyExc_ValueError, msg);
            // decref everything to let the GC touch them if it wants
            Py_DECREF(msg);
            Py_DECREF(recv_relationship);
            Py_DECREF(timestamp);
            Py_DECREF(withdraw);
            Py_DECREF(bgpsec_as_path);
            Py_DECREF(rovpp_blackhole);
            return -1;  // exit with error
        } else {    // otherwise raise NotImplementedError
            // decref so GC can touch stuff
            Py_DECREF(recv_relationship);
            Py_DECREF(timestamp);
            Py_DECREF(withdraw);
            Py_DECREF(bgpsec_as_path);
            Py_DECREF(rovpp_blackhole);
            PyErr_SetString(PyExc_NotImplementedError, ""); // set an empty pystring object as a NotImplementedError
            return -1;  // exit with error
        }
    }

    // ======================================
    // Assigning Attribute Calues and Cleanup
    // ======================================

    // assign values that were parsed
    if (Announcement_assign_fields(
            self,
            prefix,
            as_path,
            next_hop_asn,
            seed_asn,
            recv_relationship,
            timestamp,
            withdraw,
            bgpsec_next_asn,
            bgpsec_as_path,
            only_to_customers,
            rovpp_blackhole) < 0) {
        // if for some reason that fails decref what needs to be and error
        Py_DECREF(recv_relationship);
        Py_DECREF(timestamp);
        Py_DECREF(withdraw);
        Py_DECREF(bgpsec_as_path);
        Py_DECREF(rovpp_blackhole);
        return -1;  // return error
    }

    /* decref values we don't want anymore */
    Py_DECREF(recv_relationship);
    Py_DECREF(timestamp);
    Py_DECREF(withdraw);
    Py_DECREF(bgpsec_as_path);
    Py_DECREF(rovpp_blackhole);
    return 0;   // return no errors
}

/*
 * This function is equivilent to `copy(self, overwrite_default_kwargs) -> "Announcement"`
 * 
 * 
*/
static PyObject *Announcement_copy(AnnouncementObject *self, PyObject *args, PyObject *kwargs) {
    /* 
     * TODO: Reimplement this more efficiently
     * Currently this basically just makes a dict of values
     * and then calls __init__ with those values as arguements.
     * 
     * 
     * I'm not sure any fancy tricks like memset would work
     * but needless to say the way i'm doing this right now
     * is really slow compared to how it could be.
     * 
     * I think an appropriate way to do it would be to do
     * pure C field by field cloning of values with 
     * refcounts and defaults obviously. This approach would
     * be a lot faster I just have to make sure it retains
     * Python parity before doing it.
    */

    // ========================================
    // Set Up pre-reqs and Initialize Variables
    // ========================================

    // create null terminated list of keywords that we want to parse from arguements
    static char *kwlist[] = {"overwrite_default_kwargs", NULL};
    PyObject *overrides = Py_None;  // set a default value for overrides as a Py_None

    // parse arguements into overrides variable
    // `|O` just means that it's one arguement we want and it's optional
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &overrides)) {
        return NULL;    // if that fails return NULL
    }

    // ==================================
    // Copy Values To Dict For Arguements
    // ==================================

    // create a Python dict
    PyObject *ctor_kwargs = PyDict_New();
    if (!ctor_kwargs) {
        return NULL;
    }

    // Set all attributes in the dict so we can use as arguements
    if (PyDict_SetItemString(ctor_kwargs, "prefix", self->prefix) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "as_path", self->as_path) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "next_hop_asn", self->next_hop_asn) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "seed_asn", self->seed_asn) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "recv_relationship", self->recv_relationship) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "timestamp", self->timestamp) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "withdraw", self->withdraw) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "bgpsec_next_asn", self->bgpsec_next_asn) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "bgpsec_as_path", self->bgpsec_as_path) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "only_to_customers", self->only_to_customers) < 0 ||
        PyDict_SetItemString(ctor_kwargs, "rovpp_blackhole", self->rovpp_blackhole) < 0) {
        Py_DECREF(ctor_kwargs);
        return NULL;
    }

    /* merge overrides with ctor_kwargs */

    // check for overrides
    if (overrides != Py_None) {
        // if not none make sure it has elements
        int is_true = PyObject_IsTrue(overrides);
        if (is_true < 0) {
            // if there really isn't then decref and return
            Py_DECREF(ctor_kwargs);
            return NULL;
        }
        if (is_true) {
            // if it was true make sure it's a dict
            if (!PyDict_Check(overrides)) {
                // if it wasn't a dict then decref and error
                Py_DECREF(ctor_kwargs);
                PyErr_SetString(    // give the user an error
                    PyExc_TypeError,
                    "overwrite_default_kwargs must be a dict or None"
                );
                return NULL;
            }
            // if all is valid then update the ctor_kwargs with the overrides
            if (PyDict_Update(ctor_kwargs, overrides) < 0) {
                // if that fails then decref and return
                Py_DECREF(ctor_kwargs);
                return NULL;
            }
        }
    }

    // ===============================
    // Create Announcement With Values
    // ===============================

    // make a new object with the values from ctor_kwargs
    // this is equivilent to calling __init__ basicaly
    PyObject *result = PyObject_Call(
        (PyObject *)Py_TYPE(self),
        empty_tuple_singleton,
        ctor_kwargs
    );
    Py_DECREF(ctor_kwargs); // decref the values dict so GC can touch
    // return new object
    return result;
}

/*
 * returns a Python string version of an annoucement
 * equivilent to `__str__(self) -> str`
 */
static PyObject *Announcement_str(AnnouncementObject *self) {
    return PyUnicode_FromFormat("%S %R %S", self->prefix, self->as_path, self->recv_relationship);
}

// returns a python string representation of an announcement class
static PyObject *Announcement_repr(AnnouncementObject *self) {
    return PyUnicode_FromFormat(
        "Announcement(prefix=%R, as_path=%R, next_hop_asn=%R, seed_asn=%R, "
        "recv_relationship=%R, timestamp=%R, withdraw=%R, bgpsec_next_asn=%R, "
        "bgpsec_as_path=%R, only_to_customers=%R, rovpp_blackhole=%R)",
        self->prefix,
        self->as_path,
        self->next_hop_asn,
        self->seed_asn,
        self->recv_relationship,
        self->timestamp,
        self->withdraw,
        self->bgpsec_next_asn,
        self->bgpsec_as_path,
        self->only_to_customers,
        self->rovpp_blackhole
    );
}

/*
 * Returns the last item (origin) of an `as_path`
 * equivelent to `origin(self) -> int`
 */
static PyObject *Announcement_get_origin(AnnouncementObject *self, void *Py_UNUSED(closure)) {
    return PySequence_GetItem(self->as_path, -1);
}

// === optional things for testing need yaml ===

// ######################
// # Yaml Functionality #
// ######################

/* 
 * returns a python dict of all the attributes and stuff of an announcement
 * This optional method is called when you call yaml.dump()
 * equivelent to `__to_yaml_dict__(self) -> dict[str, Any]`
 */
static PyObject *Announcement_to_yaml_dict(AnnouncementObject *self, PyObject *Py_UNUSED(ignored)) {
    // create a new python dict as return value
    PyObject *out = PyDict_New();
    if (!out) {
        // if creating the dict failed return
        return NULL;
    }

    // add all values from self to dict along with string labels
    if (PyDict_SetItemString(out, "prefix", self->prefix) < 0 ||
        PyDict_SetItemString(out, "as_path", self->as_path) < 0 ||
        PyDict_SetItemString(out, "next_hop_asn", self->next_hop_asn) < 0 ||
        PyDict_SetItemString(out, "seed_asn", self->seed_asn) < 0 ||
        PyDict_SetItemString(out, "recv_relationship", self->recv_relationship) < 0 ||
        PyDict_SetItemString(out, "timestamp", self->timestamp) < 0 ||
        PyDict_SetItemString(out, "withdraw", self->withdraw) < 0 ||
        PyDict_SetItemString(out, "bgpsec_next_asn", self->bgpsec_next_asn) < 0 ||
        PyDict_SetItemString(out, "bgpsec_as_path", self->bgpsec_as_path) < 0 ||
        PyDict_SetItemString(out, "only_to_customers", self->only_to_customers) < 0 ||
        PyDict_SetItemString(out, "rovpp_blackhole", self->rovpp_blackhole) < 0) {
        // if that fails decref and exit
        Py_DECREF(out);
        return NULL;
    }

    // return the final result dict
    return out;
}

/* 
 * calls `cls(**dct)` just does python call tbh
 * This optional method is called when you call yaml.load()
 * equivelent to `__from_yaml_dict__`
 */
static PyObject *Announcement_from_yaml_dict(PyObject *cls, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"dct", "yaml_tag", NULL};
    PyObject *dct = NULL;
    PyObject *yaml_tag = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O", kwlist, &dct, &yaml_tag)) {
        return NULL;
    }
    if (!PyDict_Check(dct)) {
        PyErr_SetString(PyExc_TypeError, "dct must be a dict");
        return NULL;
    }
    return PyObject_Call(cls, empty_tuple_singleton, dct);
}

// ############################
// # Depricated Functionality #
// ############################

// Note: I still included these for the sake of it, they still warn like they did in python

/* 
 * returns a Py_True or Py_False based on equivelece of prefic and as_path
 * Checks prefix and as path equivalency
 * equivelent to `prefix_path_attributes_eq(self, ann: Optional["Announcement"]) -> bool`
 */
static PyObject *Announcement_prefix_path_attributes_eq(
    AnnouncementObject *self,
    PyObject *ann
) {
    // create a message for deprication warning
    const char *msg =
        "Please use (ann.prefix, ann.as_path) == (self.prefix, self.as_path) "
        "instead of ._ribs_out. This will be removed in a later version";

    // send deprication warning
    if (PyErr_WarnEx(PyExc_DeprecationWarning, msg, 2) < 0) {
        return NULL;
    }

    // if the other object is None then return false
    if (ann == Py_None) {
        Py_RETURN_FALSE;
    }

    // if the other object is an announcement
    if (PyObject_TypeCheck(ann, &AnnouncementType)) {
        // cast it as an announcement
        AnnouncementObject *other = (AnnouncementObject *)ann;
        // compare prefixes
        int eq_prefix = PyObject_RichCompareBool(self->prefix, other->prefix, Py_EQ);
        if (eq_prefix < 0) {
            return NULL;    // < 0 would signify an error in comparison so return
        }
        if (!eq_prefix) {   // = 0 would be false
            Py_RETURN_FALSE;
        }
        // compare as_paths
        int eq_path = PyObject_RichCompareBool(self->as_path, other->as_path, Py_EQ);
        if (eq_path < 0) {
            return NULL;    // < 0 would signify an error in comparison so return
        }
        if (eq_path) {  // = 1 would be true
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;    // by default return false
    }

    // raise a not implemented error if we reach the bottom of the function
    PyErr_SetString(PyExc_NotImplementedError, "");
    return NULL;    // return null
}

/* 
 * return Py_True or Py_False based on bgpsec_next_asn and bgpsec_as_path
 * Returns True if valid by BGPSec else False
 * equivelent to `bgpsec_valid(self, asn: int) -> bool`
 */
static PyObject *Announcement_bgpsec_valid(AnnouncementObject *self, PyObject *asn) {
    // create an error message for deprication
    const char *msg =
        "Please call bgpsec_valid from the BGPSec class, not the Announcement. "
        "This will be removed in a later version";

    // send deptrication warning at stacklevel 2
    if (PyErr_WarnEx(PyExc_DeprecationWarning, msg, 2) < 0) {
        return NULL;
    }

    // compare bgpsec_next_asn == asn
    int first = PyObject_RichCompareBool(self->bgpsec_next_asn, asn, Py_EQ);
    if (first < 0) {
        return NULL;    // < 0 would signify an error in comparison so return
    }
    if (!first) {
        Py_RETURN_FALSE;    // = 0 would be false
    }

    // compare bgpsec_as_path == as_path
    int second = PyObject_RichCompareBool(self->bgpsec_as_path, self->as_path, Py_EQ);
    if (second < 0) {
        return NULL;    // < 0 would signify an error in comparison so return
    }
    if (second) {
        Py_RETURN_TRUE; // = 1 would be true
    }
    Py_RETURN_FALSE;    // return false by default
}

// === End of optional ===

// ########################
// # Python Functionality #
// ########################

/* 
 * implement rich comparison (1 if equal 0 if not)
 * returns a Py_True or Py_false
 */
static PyObject *Announcement_richcompare(PyObject *a, PyObject *b, int op) {
    // make sure this is being used correctly
    if (op != Py_EQ && op != Py_NE) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    // make sure both objects are of AnnouncementType
    if (!PyObject_TypeCheck(a, &AnnouncementType) || !PyObject_TypeCheck(b, &AnnouncementType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    // make sure both announcements are the same type
    if (Py_TYPE(a) != Py_TYPE(b)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    // cast them both as the correct type
    AnnouncementObject *left = (AnnouncementObject *)a;
    AnnouncementObject *right = (AnnouncementObject *)b;

    // compare all attributes, 1 if all equal 0 else
    int eq =    // Note: this can probably be made quicker by just using C comparison in the future
        PyObject_RichCompareBool(left->prefix, right->prefix, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->as_path, right->as_path, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->next_hop_asn, right->next_hop_asn, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->seed_asn, right->seed_asn, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->recv_relationship, right->recv_relationship, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->timestamp, right->timestamp, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->withdraw, right->withdraw, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->bgpsec_next_asn, right->bgpsec_next_asn, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->bgpsec_as_path, right->bgpsec_as_path, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->only_to_customers, right->only_to_customers, Py_EQ) == 1 &&
        PyObject_RichCompareBool(left->rovpp_blackhole, right->rovpp_blackhole, Py_EQ) == 1;

    // if an error occured we should return now
    if (PyErr_Occurred()) {
        return NULL;
    }

    // if operation is equality
    if (op == Py_EQ) {
        if (eq) {   // return Py_true if they are equal
            Py_RETURN_TRUE;
        }
        // return Py_False if not
        Py_RETURN_FALSE;
    }
    // otherwise we are doing a not equal
    if (eq) {   // return Py_False if they are equal
        Py_RETURN_FALSE;
    }
    // return Py_true if they are different
    Py_RETURN_TRUE;
}

/* 
 * Performs a hash on an announcement
 * returns a `Py_hash_t` type
 */
static Py_hash_t Announcement_hash(AnnouncementObject *self) {
    // if it's already computed return the present hash
    if (self->hash_computed) {
        return self->hash_cache;
    }

    // put all attributes of announcement into a tuple
    PyObject *items = PyTuple_Pack(
        11,
        self->prefix,
        self->as_path,
        self->next_hop_asn,
        self->seed_asn,
        self->recv_relationship,
        self->timestamp,
        self->withdraw,
        self->bgpsec_next_asn,
        self->bgpsec_as_path,
        self->only_to_customers,
        self->rovpp_blackhole
    );
    if (!items) {   // if there was an error with creating the tuple return with error
        return -1;
    }

    // hash the tuple of attributes
    Py_hash_t h = PyObject_Hash(items);
    // decref the items tuple so the GC can touch it
    Py_DECREF(items);

    // if the hash failed then return -1
    if (h == -1) {
        return -1;
    }

    // set hash already computed flag and store hash
    self->hash_cache = h;
    self->hash_computed = 1;

    // return the hash
    return h;
}

// create a null terminated list of Announcement members with basic descriptions, this is for creating AnnouncementType
static PyMemberDef Announcement_members[] = {
    {"prefix", T_OBJECT_EX, offsetof(AnnouncementObject, prefix), READONLY, "IP prefix"},
    {"as_path", T_OBJECT_EX, offsetof(AnnouncementObject, as_path), READONLY, "AS path tuple"},
    {"next_hop_asn", T_OBJECT_EX, offsetof(AnnouncementObject, next_hop_asn), READONLY, "Next hop ASN"},
    {"seed_asn", T_OBJECT_EX, offsetof(AnnouncementObject, seed_asn), READONLY, "Seed ASN"},
    {"recv_relationship", T_OBJECT_EX, offsetof(AnnouncementObject, recv_relationship), READONLY, "Receive relationship"},
    {"timestamp", T_OBJECT_EX, offsetof(AnnouncementObject, timestamp), READONLY, "Timestamp"},
    {"withdraw", T_OBJECT_EX, offsetof(AnnouncementObject, withdraw), READONLY, "Withdraw flag"},
    {"bgpsec_next_asn", T_OBJECT_EX, offsetof(AnnouncementObject, bgpsec_next_asn), READONLY, "BGPSec next ASN"},
    {"bgpsec_as_path", T_OBJECT_EX, offsetof(AnnouncementObject, bgpsec_as_path), READONLY, "BGPSec AS path"},
    {"only_to_customers", T_OBJECT_EX, offsetof(AnnouncementObject, only_to_customers), READONLY, "OTC attribute"},
    {"rovpp_blackhole", T_OBJECT_EX, offsetof(AnnouncementObject, rovpp_blackhole), READONLY, "ROV++ blackhole flag"},
    {NULL}
};

// create a null terminated list with the getter for announcement, returns origin (no setter)
static PyGetSetDef Announcement_getset[] = {
    {"origin", (getter)Announcement_get_origin, NULL, "Returns the origin of the announcement", NULL},
    {NULL}
};

// create a null terminated list of methods for announcements, set propper names, set propper descriptions
static PyMethodDef Announcement_methods[] = {
    // === optional for yaml and testing ===
    {"__to_yaml_dict__", (PyCFunction)Announcement_to_yaml_dict, METH_NOARGS, "yaml.dump helper"},
    {"__from_yaml_dict__", (PyCFunction)Announcement_from_yaml_dict, METH_VARARGS | METH_KEYWORDS | METH_CLASS, "yaml.load helper"},
    {"prefix_path_attributes_eq", (PyCFunction)Announcement_prefix_path_attributes_eq, METH_O, "Checks prefix and as path equivalency"},
    {"bgpsec_valid", (PyCFunction)Announcement_bgpsec_valid, METH_O, "Returns True if valid by BGPSec else False"},
    // === optional for yaml and testting ===
    {"copy", (PyCFunction)Announcement_copy, METH_VARARGS | METH_KEYWORDS, "Creates a new ann with proper sim attrs"},
    {NULL, NULL, 0, NULL}
};

// creates the AnnouncementType from everything we have put together so far
static PyTypeObject AnnouncementType = {
    PyVarObject_HEAD_INIT(NULL, 0)  // initialize the head
    .tp_name = "bgpy.simulation_engine._announcement_c.Announcement",   // set name
    .tp_doc = "C backed BGP Announcement",  // set temporary docstring
    .tp_basicsize = sizeof(AnnouncementObject), // set the size (size of struct)
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   // Important allow for subclassing
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Announcement_init, // set initialization function
    .tp_dealloc = (destructor)Announcement_dealloc, // set deallocation function
    .tp_setattro = Announcement_setattro,   // set setattrobute function (this just warns that you can't)
    .tp_members = Announcement_members, // set the members from list above
    .tp_getset = Announcement_getset,   // set the getter from list above
    .tp_methods = Announcement_methods, // set the methods from list above
    .tp_str = (reprfunc)Announcement_str,   // set the __str__ function
    .tp_repr = (reprfunc)Announcement_repr, // set the __repr__ function (I think that's what it's called)
    .tp_richcompare = Announcement_richcompare, // set the comparison function
    .tp_hash = (hashfunc)Announcement_hash, // set the hash function
};

// create a struct for the module
static struct PyModuleDef announcement_c_module = {
    PyModuleDef_HEAD_INIT,  // initialize compatability head
    "_announcement_c",      // give it a name
    "C extension for simulation_engine Announcement",   // description
    -1,
    NULL
};

/* 
 * This function initialized the module, it is called when you import the module
 * and sets up everything to be used
 */
PyMODINIT_FUNC PyInit__announcement_c(void) {
    // initialize all needed module level objects as NULL
    PyObject *enums_mod = NULL;
    PyObject *relationships_cls = NULL;
    PyObject *module = NULL;

    // import the enums module so we can fetch Relationships.ORIGIN default
    enums_mod = PyImport_ImportModule("bgpy.shared.enums");
    // if import fails just return NULL so python raises import error
    if (!enums_mod) {
        return NULL;
    }
    // get the Relationships class from bgpy.shared.enums
    relationships_cls = PyObject_GetAttrString(enums_mod, "Relationships");
    // no longer need enums_mod after fetching class
    Py_DECREF(enums_mod);
    // if getting Relationships failed then error out
    if (!relationships_cls) {
        return NULL;
    }
    // fetch Relationships.ORIGIN and cache globally for default recv_relationship
    relationships_origin = PyObject_GetAttrString(relationships_cls, "ORIGIN");
    // done with class object now
    Py_DECREF(relationships_cls);
    // if ORIGIN attr wasn't found then error out
    if (!relationships_origin) {
        return NULL;
    }

    // create and cache a single empty tuple so we don't reallocate repeatedly
    empty_tuple_singleton = PyTuple_New(0);
    // if tuple allocation failed clean up previously created globals
    if (!empty_tuple_singleton) {
        Py_DECREF(relationships_origin);
        return NULL;
    }

    // finalize the Announcement type object before adding it to module
    if (PyType_Ready(&AnnouncementType) < 0) {
        Py_DECREF(relationships_origin);
        Py_DECREF(empty_tuple_singleton);
        return NULL;
    }

    // create the extension module object
    module = PyModule_Create(&announcement_c_module);
    // if module creation fails clean up globals
    if (!module) {
        Py_DECREF(relationships_origin);
        Py_DECREF(empty_tuple_singleton);
        return NULL;
    }

    // incref type because PyModule_AddObject steals a reference on success
    Py_INCREF(&AnnouncementType);
    // expose Announcement type as module attribute "Announcement"
    if (PyModule_AddObject(module, "Announcement", (PyObject *)&AnnouncementType) < 0) {
        // if add fails undo incref and decref everything we own
        Py_DECREF(&AnnouncementType);
        Py_DECREF(module);
        Py_DECREF(relationships_origin);
        Py_DECREF(empty_tuple_singleton);
        return NULL;
    }

    // return initialized module object to python import system
    return module;
}
