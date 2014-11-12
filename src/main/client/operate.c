/*******************************************************************************
 * Copyright 2013-2014 Aerospike, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#include <Python.h>
#include <stdbool.h>
#include <string.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/as_key.h>
#include <aerospike/as_error.h>
#include <aerospike/as_record.h>

#include "client.h"
#include "conversions.h"
#include "key.h"
#include "policy.h"

PyObject *  AerospikeClient_Operate_Invoke(
    AerospikeClient * self,
    as_key * key, PyObject * py_bin, char* val, as_error * err, long ttl,
    long initial_value, long offset, long operation, as_operations * ops)
{
    char* bin = NULL;

    switch(operation) {
        case AS_OPERATOR_APPEND:
            if( !PyString_Check(py_bin) ) {
                as_error_update(err, AEROSPIKE_ERR_PARAM, "Bin should be a string");
                goto CLEANUP;
            }
            bin = PyString_AsString(py_bin);
            as_operations_add_append_str(ops, bin, val);
            break;
            
        case AS_OPERATOR_PREPEND:
            if( !PyString_Check(py_bin) ) {
                as_error_update(err, AEROSPIKE_ERR_PARAM, "Bin should be a string");
                goto CLEANUP;
            }
            bin = PyString_AsString(py_bin);
            as_operations_add_prepend_str(ops, bin, val);
            break;

        case AS_OPERATOR_INCR:
            if( !PyString_Check(py_bin) ) {
                as_error_update(err, AEROSPIKE_ERR_PARAM, "Bin should be a string");
                goto CLEANUP;
            }
            bin = PyString_AsString(py_bin);
            as_val* value_p = NULL;
            const char* select[] = {bin, NULL};
            as_record* get_rec = NULL;
            aerospike_key_select(self->as,
                    err, NULL, key, select, &get_rec);
            if (err->code != AEROSPIKE_OK) {
                goto CLEANUP;
            }
            else 
            {
                if (NULL != (value_p = (as_val *) as_record_get (get_rec, bin))) {
                    if (AS_NIL == value_p->type) {
                        if (!as_operations_add_write_int64(ops, bin,
                                    initial_value)) {
                            goto CLEANUP;
                        }
                    } else {
                        as_operations_add_incr(ops, bin, offset);
                    }
                } else {
                    goto CLEANUP;
                }
            }
            break;

        case AS_OPERATOR_TOUCH:
            ops->ttl = ttl;
            as_operations_add_touch(ops);
            break;

        case AS_OPERATOR_READ:
            if( !PyString_Check(py_bin) ) {
                as_error_update(err, AEROSPIKE_ERR_PARAM, "Bin should be a string");
                goto CLEANUP;
            }
            bin = PyString_AsString(py_bin);
            as_operations_add_read(ops, bin);
            break;

        case AS_OPERATOR_WRITE:
            if( !PyString_Check(py_bin) ) {
                as_error_update(err, AEROSPIKE_ERR_PARAM, "Bin should be a string");
                goto CLEANUP;
            }
            bin = PyString_AsString(py_bin);
            if (val) {
                as_operations_add_write_str(ops, bin, val);
            } else {
                as_operations_add_write_int64(ops, bin, offset);
            }
            break;
	}

CLEANUP:
    if ( err->code != AEROSPIKE_OK ) {
        PyObject * py_err = NULL;
        error_to_pyobject(err, &py_err);
        PyErr_SetObject(PyExc_Exception, py_err);
        return NULL;
    }
    return PyLong_FromLong(0);
}

PyObject * AerospikeClient_convert_pythonObj_to_asType(
        AerospikeClient * self,
        as_error *err, PyObject* py_key, PyObject* py_policy,
        as_key* key_p, as_policy_operate* operate_policy_pp)
{
    pyobject_to_key(err, py_key, key_p);
    if ( err->code != AEROSPIKE_OK ) {
        goto CLEANUP;
    }

    if (py_policy) {
    // Convert python policy object to as_policy_operate
    pyobject_to_policy_operate(err, py_policy, operate_policy_pp, &operate_policy_pp);
    if ( err->code != AEROSPIKE_OK ) {
        goto CLEANUP;
	}
    }

CLEANUP:
	 if ( err->code != AEROSPIKE_OK ) {
        PyObject * py_err = NULL;
        error_to_pyobject(err, &py_err);
        PyErr_SetObject(PyExc_Exception, py_err);
        return NULL;
    }
	return PyLong_FromLong(0);
}

PyObject * AerospikeClient_Append(AerospikeClient * self, PyObject * args, PyObject * kwds)
{
    // Initialize error
    as_error err;
    as_error_init(&err);

    // Python Function Arguments
    PyObject * py_key = NULL;
    PyObject * py_bin = NULL;
    PyObject * py_policy = NULL;
    PyObject * py_result = NULL;
    char* append_str = NULL;

    as_operations ops;
    as_policy_operate operate_policy;
    as_key key;

    // Python Function Keyword Arguments
    static char * kwlist[] = {"key", "bin", "val", "policy", NULL};

    // Python Function Argument Parsing
    if ( PyArg_ParseTupleAndKeywords(args, kwds, "OOs|O:append", kwlist,
							&py_key, &py_bin, &append_str, &py_policy) == false ) {
        return NULL;
    }

    as_operations_inita(&ops, 1);

    if (py_policy) {
        set_policy_operate(&err, py_policy, &operate_policy);
    }
    if (err.code != AEROSPIKE_OK) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_convert_pythonObj_to_asType(self, &err,
            py_key, py_policy, &key, &operate_policy);
    if (!py_result) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_Operate_Invoke(self, &key, py_bin, append_str,
            &err, 0, 0, 0, AS_OPERATOR_APPEND, &ops);
    if (py_result)
    {
        if (py_policy) {
            aerospike_key_operate(self->as, &err, &operate_policy, &key, &ops, NULL);
        } else {
            aerospike_key_operate(self->as, &err, NULL, &key, &ops, NULL);
        }
        if (err.code != AEROSPIKE_OK) {
            goto CLEANUP;
        }
    }
    else
        goto CLEANUP;

CLEANUP:
    if ( err.code != AEROSPIKE_OK ) {
        PyObject * py_err = NULL;
        error_to_pyobject(&err, &py_err);
        PyErr_SetObject(PyExc_Exception, py_err);
        return NULL;
    }
    return PyLong_FromLong(0);
}

PyObject * AerospikeClient_Prepend(AerospikeClient * self, PyObject * args, PyObject * kwds)
{
    // Initialize error
    as_error err;
    as_error_init(&err);

    // Python Function Arguments
    PyObject * py_key = NULL;
    PyObject * py_bin = NULL;
    PyObject * py_policy = NULL;
    PyObject * py_result = NULL;

    char* prepend_str = NULL;

    as_operations ops;
    as_policy_operate operate_policy;
    as_key key;

    as_operations_inita(&ops, 1);

    // Python Function Keyword Arguments
    static char * kwlist[] = {"key", "bin", "val", "policy", NULL};

    // Python Function Argument Parsing
    if ( PyArg_ParseTupleAndKeywords(args, kwds, "OOs|O:prepend", kwlist,
                &py_key, &py_bin, &prepend_str, &py_policy) == false ) {
        return NULL;
    }

    if (py_policy) {
        set_policy_operate(&err, py_policy, &operate_policy);
    }
    if (err.code != AEROSPIKE_OK) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_convert_pythonObj_to_asType(self, &err,
            py_key, py_policy, &key, &operate_policy);
    if (!py_result) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_Operate_Invoke(self, &key, py_bin, prepend_str,
            &err, 0, 0, 0, AS_OPERATOR_PREPEND, &ops);
    if (py_result)
	{
        if (py_policy) {
            aerospike_key_operate(self->as, &err, &operate_policy, &key, &ops, NULL);
        } else {
            aerospike_key_operate(self->as, &err, NULL, &key, &ops, NULL);
        }
        if (err.code != AEROSPIKE_OK) {
            goto CLEANUP;
        }
    }

CLEANUP:
    if ( err.code != AEROSPIKE_OK ) {
        PyObject * py_err = NULL;
        error_to_pyobject(&err, &py_err);
        PyErr_SetObject(PyExc_Exception, py_err);
        return NULL;
    }

    return PyLong_FromLong(0);
}

PyObject * AerospikeClient_Increment(AerospikeClient * self, PyObject * args, PyObject * kwds)
{
    // Initialize error
    as_error err;
    as_error_init(&err);

    // Python Function Arguments
    PyObject * py_key = NULL;
    PyObject * py_policy = NULL;
    PyObject * py_result = NULL;
    PyObject * py_bin = NULL;

    as_operations ops;
    as_key key;
    as_policy_operate operate_policy;

    long offset_val = 0;
    long initial_val = 0;
    as_operations_inita(&ops, 1);

    // Python Function Keyword Arguments
    static char * kwlist[] = {"key", "bin", "offset", "initial_value", "policy", NULL};

    // Python Function Argument Parsing
    if ( PyArg_ParseTupleAndKeywords(args, kwds, "OOl|lO:increment", kwlist, 
			&py_key, &py_bin, &offset_val, &initial_val, &py_policy) == false ) {
        return NULL;
    }

    if (py_policy) {
        set_policy_operate(&err, py_policy, &operate_policy);
    }
    if (err.code != AEROSPIKE_OK) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_convert_pythonObj_to_asType(self, &err,
                          py_key, py_policy, &key, &operate_policy);
    
    if (!py_result) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_Operate_Invoke(self, &key, py_bin, NULL,
        &err, 0, initial_val, offset_val, AS_OPERATOR_INCR, &ops);
    if (py_result)
    {
        if (py_policy) {
            aerospike_key_operate(self->as, &err, &operate_policy, &key, &ops, NULL);
        } else {
            aerospike_key_operate(self->as, &err, NULL, &key, &ops, NULL);
        }
        if (err.code != AEROSPIKE_OK) {
            goto CLEANUP;
        }
    }

CLEANUP:
	if ( err.code != AEROSPIKE_OK ) {
        PyObject * py_err = NULL;
        error_to_pyobject(&err, &py_err);
        PyErr_SetObject(PyExc_Exception, py_err);
        return NULL;
    }
    return PyLong_FromLong(0);
}

PyObject * AerospikeClient_Touch(AerospikeClient * self, PyObject * args, PyObject * kwds)
{
    // Initialize error
    as_error err;
    as_error_init(&err);

    // Python Function Arguments
    PyObject * py_key = NULL;
    PyObject * py_policy = NULL;
    PyObject * py_result = NULL;
    
    as_operations ops;
    as_key key;
    as_policy_operate operate_policy;
    long touchvalue = 0;

    as_operations_inita(&ops, 1);

    // Python Function Keyword Arguments
    static char * kwlist[] = {"key", "val", "policy", NULL};

    // Python Function Argument Parsing
    if ( PyArg_ParseTupleAndKeywords(args, kwds, "Ol|O:touch", kwlist, 
    		&py_key, &touchvalue, &py_policy) == false ) {
        return NULL;
    }

    if (py_policy) {
        set_policy_operate(&err, py_policy, &operate_policy);
    }
    if (err.code != AEROSPIKE_OK) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_convert_pythonObj_to_asType(self, &err,
                          py_key, py_policy, &key, &operate_policy);
    if (!py_result) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_Operate_Invoke(self, &key, NULL, NULL,
            &err, touchvalue, 0, 0, AS_OPERATOR_TOUCH, &ops);
    if (py_result)
    {
        if (py_policy) {
            aerospike_key_operate(self->as, &err, &operate_policy, &key, &ops, NULL);
        } else {
            aerospike_key_operate(self->as, &err, NULL, &key, &ops, NULL);
        }
        if (err.code != AEROSPIKE_OK) {
            goto CLEANUP;
        }
    }

CLEANUP:
    if ( err.code != AEROSPIKE_OK ) {
        PyObject * py_err = NULL;
        error_to_pyobject(&err, &py_err);
        PyErr_SetObject(PyExc_Exception, py_err);
        return NULL;
    }
    return PyLong_FromLong(0);
}

PyObject * AerospikeClient_Operate(AerospikeClient * self, PyObject * args, PyObject * kwds) {
    // Initialize error
    as_error err;
    as_error_init(&err);

    // Python Function Arguments
    PyObject * py_key = NULL;
    PyObject * py_list = NULL;
    PyObject * py_policy = NULL;
    PyObject * py_result = NULL;
    PyObject * py_rec = NULL;

    as_operations ops;
    as_policy_operate operate_policy;
    as_key key;
    as_record * rec = NULL;

    long touchvalue = 0;
    long op;
    PyObject * bin_name = NULL;
    char * str = NULL;
    long offset;

    // Initialize record
    as_record_init(rec, 0);

    // Python Function Keyword Arguments
    static char * kwlist[] = {"key", "list", "policy", NULL};

    // Python Function Argument Parsing
    if ( PyArg_ParseTupleAndKeywords(args, kwds, "OO|O:operate", kwlist,
            &py_key, &py_list, &py_policy) == false ) {
        return NULL;
    }

    if (py_policy) {
        set_policy_operate(&err, py_policy, &operate_policy);
    }
    if (err.code != AEROSPIKE_OK) {
        goto CLEANUP;
    }

    py_result = AerospikeClient_convert_pythonObj_to_asType(self, &err,
                          py_key, py_policy, &key, &operate_policy);
    if (!py_result) {
        goto CLEANUP;
    }

    if ( py_list != NULL && PyList_Check(py_list) ) {
        Py_ssize_t size = PyList_Size(py_list);
        as_operations_inita(&ops, size);
        for ( int i = 0; i < size; i++ ) {
            PyObject * py_val = PyList_GetItem(py_list, i);
            op = -1;
            str = NULL;
            bin_name = "";
            offset = 0;
            if ( PyDict_Check(py_val) ) {
                PyObject *key_op = NULL, *value = NULL;
                Py_ssize_t pos = 0;
                while (PyDict_Next(py_val, &pos, &key_op, &value)) {
                    if ( ! PyString_Check(key_op) ) {
                        as_error_update(&err, AEROSPIKE_ERR_CLIENT, "A operation key must be a string.");
                        goto CLEANUP;
                     } else {
                        char * name = PyString_AsString(key_op);
                        if(!strcmp(name,"op") && (PyInt_Check(value) || PyLong_Check(value))) {
                            op = PyInt_AsLong(value);
                        } else if (!strcmp(name, "bin") && PyString_Check(value)) {
                            bin_name = value;
                        } else if(!strcmp(name, "val")) {
                            if (PyString_Check(value)) {
                                str = PyString_AsString(value);
                            } else if (PyInt_Check(value) || PyLong_Check(value)) {
                                offset = PyInt_AsLong(value);
                            } else {
                                as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Value of incompatible type");
                                goto CLEANUP;
                            }
                        } else {
                            as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Operation of incompatible type");
                            goto CLEANUP;
                        }
                    }
            }
            py_result = AerospikeClient_Operate_Invoke(self, &key, bin_name,
                    str, &err, offset, offset, offset, op, &ops);
        }
    }
    } else {
        as_error_update(&err, AEROSPIKE_ERR_PARAM, "Operations should be a list");
        goto CLEANUP;
    }
    if (py_result)
    {
        if (py_policy) {
            aerospike_key_operate(self->as, &err, &operate_policy, &key, &ops, &rec);
        } else {
            aerospike_key_operate(self->as, &err, NULL, &key, &ops, &rec);
        }
        if (err.code != AEROSPIKE_OK) {
            goto CLEANUP;
        }
        if(rec) {
            record_to_pyobject(&err, rec, &key, &py_rec);
            return py_rec;
        }
    }
CLEANUP:
    as_record_destroy(rec);
    if ( err.code != AEROSPIKE_OK ) {
        PyObject * py_err = NULL;
        error_to_pyobject(&err, &py_err);
        PyErr_SetObject(PyExc_Exception, py_err);
        return NULL;
    }
    return PyLong_FromLong(0);
}
