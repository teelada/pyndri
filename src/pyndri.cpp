#include <Python.h>
#include "structmember.h"

#include <cassert>
#include <string>
#include <iostream>

#include <indri/CompressedCollection.hpp>
#include <indri/DiskIndex.hpp>
#include <indri/KrovetzStemmer.hpp>
#include <indri/QueryEnvironment.hpp>
#include <indri/Path.hpp>
#include "indri/SnippetBuilder.hpp"

using std::string;

#define ENCODING "latin1"

#define CHECK(condition) assert(condition)
#define CHECK_EQ(first, second) assert(first == second)
#define CHECK_GT(first, second) assert(first > second)
#define CHECK_GE(first, second) assert(first >= second)

// Index

typedef struct {
    PyObject_HEAD

    indri::api::Parameters* parameters_;

    indri::collection::CompressedCollection* collection_;
    indri::index::DiskIndex* index_;

    indri::api::QueryEnvironment* query_env_;
} Index;

static void Index_dealloc(Index* self) {
    // self->collection_->close();
    self->index_->close();
    // self->query_env_->close();

    delete self->parameters_;

    // delete self->collection_;
    delete self->index_;
    delete self->query_env_;
}

static PyObject* Index_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    Index* self;

    self = (Index*) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->parameters_ = new indri::api::Parameters;

        self->collection_ = new indri::collection::CompressedCollection;
        self->index_ = new indri::index::DiskIndex;

        self->query_env_ = new indri::api::QueryEnvironment;
    }

    return (PyObject*) self;
}

static int Index_init(Index* self, PyObject* args, PyObject* kwds) {
    char* repository_path = "";

    static char* kwlist[] = {"repository_path", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist,
                                     &repository_path)) {
        return -1;
    }

    // Load parameters.
    self->parameters_->loadFile(indri::file::Path::combine(repository_path, "manifest"));

    const std::string collection_path =
        indri::file::Path::combine(repository_path, "collection");

    try {
        self->collection_->open(collection_path);
    } catch (const lemur::api::Exception& e) {
        PyErr_SetString(PyExc_IOError, e.what().c_str());

        return -1;
    }

    // Load index.
    std::string index_path = "index";

    indri::api::Parameters container = (*self->parameters_)["indexes"];

    if(container.exists("index")) {
        indri::api::Parameters indexes = container["index"];

        if (indexes.size() != 1) {
            PyErr_SetString(PyExc_IOError, "Indri repository contain more than one index.");

            return -1;
        }

        index_path = indri::file::Path::combine(
            index_path, (std::string) indexes[static_cast<size_t>(0)]);
    } else {
        PyErr_SetString(PyExc_IOError, "Indri repository does not contain an index.");

        return -1;
    }

    try {
        self->index_->open(repository_path, index_path);
    } catch (const lemur::api::Exception& e) {
        PyErr_SetString(PyExc_IOError, e.what().c_str());

        return -1;
    }

    try {
        self->query_env_->addIndex(repository_path);
    } catch (const lemur::api::Exception& e) {
        PyErr_SetString(PyExc_IOError, e.what().c_str());

        return -1;
    }

    return 0;
}

static PyMemberDef Index_members[] = {
    {NULL}  /* Sentinel */
};

static PyObject* Index_get_document_ids(Index* self, PyObject* args) {
    PyObject* external_doc_ids = NULL;

    if (!PyArg_ParseTuple(args, "O", &external_doc_ids)) {
        return NULL;
    }

    if (external_doc_ids == NULL) {
        return NULL;
    }

    PyObject* const iterator = PyObject_GetIter(external_doc_ids);
    PyObject *item;

    if (iterator == NULL) {
        PyErr_SetString(
            PyExc_TypeError,
            "Passed object is not iterable.");

        Py_DECREF(iterator);

        return NULL;
    }

    std::vector<std::string> ext_document_ids;

    while (item = PyIter_Next(iterator)) {
        CHECK(PyUnicode_CheckExact(item));

        PyObject* item_bytes = PyUnicode_AsEncodedString(item, ENCODING, "strict");
        char* const ext_document_id = PyBytes_AsString(item_bytes);

        // Create a copy of the string.
        ext_document_ids.push_back(ext_document_id);

        Py_DECREF(item_bytes);
        Py_DECREF(item);
    }

    Py_DECREF(iterator);

    std::vector<lemur::api::DOCID_T> int_doc_ids;

    try {
        int_doc_ids = self->query_env_->documentIDsFromMetadata("docno", ext_document_ids);
    } catch (const lemur::api::Exception& e) {
        PyErr_SetString(PyExc_IOError, e.what().c_str());

        return NULL;
    }

    PyObject* const doc_ids_tuple = PyTuple_New(int_doc_ids.size());

    Py_ssize_t pos = 0;
    for (std::vector<lemur::api::DOCID_T>::iterator int_doc_ids_it = int_doc_ids.begin();
         int_doc_ids_it != int_doc_ids.end();
         ++int_doc_ids_it, ++pos) {
        const lemur::api::DOCID_T int_document_id = *int_doc_ids_it;

        std::string ext_document_id;

        try {
            ext_document_id = self->collection_->retrieveMetadatum(int_document_id, "docno");
        } catch (const lemur::api::Exception& e) {
            PyErr_SetString(PyExc_IOError, e.what().c_str());

            Py_DECREF(doc_ids_tuple);

            return NULL;
        }

        PyTuple_SetItem(doc_ids_tuple,
                        pos,
                        PyTuple_Pack(2,
                            PyUnicode_Decode(ext_document_id.c_str(),
                                             ext_document_id.size(),
                                             ENCODING,
                                             "strict"),
                            PyLong_FromLong(int_document_id)));
    }

    return doc_ids_tuple;
}

static PyObject* Index_document(Index* self, PyObject* args) {
    int int_document_id;

    if (!PyArg_ParseTuple(args, "i", &int_document_id)) {
        return NULL;
    }

    if (int_document_id < self->index_->documentBase() ||
        int_document_id >= self->index_->documentMaximum()) {
        PyErr_SetString(
            PyExc_IndexError,
            "Specified internal document identifier is out of bounds.");

        return NULL;
    }

    string ext_document_id;
    const indri::index::TermList* term_list = 0;

    try {
        ext_document_id = self->collection_->retrieveMetadatum(
            int_document_id, "docno");
        term_list = self->index_->termList(int_document_id);
    } catch (const lemur::api::Exception& e) {
        PyErr_SetString(PyExc_IOError, e.what().c_str());

        if (term_list != 0) {
            delete term_list;
        }

        return NULL;
    }

    PyObject* terms = PyTuple_New(term_list->terms().size());

    indri::utility::greedy_vector<lemur::api::TERMID_T>::const_iterator term_it =
        term_list->terms().begin();

    Py_ssize_t pos = 0;
    for (; term_it != term_list->terms().end(); ++term_it, ++pos) {
        PyTuple_SetItem(terms, pos, PyLong_FromLong(*term_it));
    }

    delete term_list;

    return PyTuple_Pack(
        2,
        PyUnicode_Decode(ext_document_id.c_str(),
                         ext_document_id.size(),
                         ENCODING,
                         "strict"),
        terms);
}

static PyObject* Index_document_base(Index* self) {
    return PyLong_FromLong(self->index_->documentBase());
}

static PyObject* Index_maximum_document(Index* self) {
    return PyLong_FromLong(self->index_->documentMaximum());
}

static PyObject* Index_document_count(Index* self) {
    return PyLong_FromLong(self->index_->documentCount());
}

static PyObject* Index_total_terms(Index* self) {
    return PyLong_FromLong(self->index_->termCount());
}

static PyObject* Index_unique_terms(Index* self) {
    return PyLong_FromLong(self->index_->uniqueTermCount());
}

static PyObject* Index_term_count(Index* self, PyObject* args) {
    char* term_object;

    if (!PyArg_ParseTuple(args, "s", &term_object)) {
        return NULL;
    }

    return PyLong_FromLong(self->index_->termCount(term_object));
}

static PyObject* Index_document_length(Index* self, PyObject* args) {
    int int_document_id;

    if (!PyArg_ParseTuple(args, "i", &int_document_id)) {
        return NULL;
    }

    return PyLong_FromLong(self->index_->documentLength(int_document_id));
}

static PyObject* Index_run_query(Index* self, PyObject* args, PyObject* kwds) {
    PyObject* query = NULL;
    PyObject* document_set = NULL;
    long results_requested = 0;
    bool include_snippets = false;

    static char* kwlist[] = {"query_str",
                             "document_set",
                             "results_requested",
                             "include_snippets",
                             NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "U|Olb", kwlist,
                                     &query,
                                     &document_set,
                                     &results_requested,
                                     &include_snippets)) {
        return NULL;
    }

    CHECK(PyUnicode_Check(query));

    PyObject* query_bytes = PyUnicode_AsEncodedString(query, ENCODING, "strict");
    char* query_str = PyBytes_AsString(query_bytes);

    std::vector<lemur::api::DOCID_T> document_ids;

    if (document_set != NULL) {
        CHECK(PyIter_Check(document_set));

        PyObject* const iterator = PyObject_GetIter(document_set);
        PyObject *item;

        if (iterator == NULL) {
            PyErr_SetString(
                PyExc_TypeError,
                "Passed object for document_set not iterable.");

            Py_DECREF(iterator);
            Py_DECREF(query_bytes);

            return NULL;
        }

        while (item = PyIter_Next(iterator)) {
            CHECK(PyLong_CheckExact(item));

            const lemur::api::DOCID_T int_doc_id = PyLong_AsLong(item);
            if (int_doc_id < 0) {
                continue;
            }

            document_ids.push_back(int_doc_id);

            Py_DECREF(item);
        }

        Py_DECREF(iterator);
    }

    if (results_requested <= 0) {
        if (document_set != NULL) {
            results_requested = document_ids.size();
        } else {
            results_requested = 100;
        }
    }

    CHECK_GE(results_requested, 0);

    indri::api::QueryAnnotation* query_annotation;

    try {
        if (document_ids.empty()) {
            query_annotation = self->query_env_->runAnnotatedQuery(
                query_str, results_requested);
        } else{
            query_annotation = self->query_env_->runAnnotatedQuery(
                query_str, document_ids, results_requested);
        }
    } catch (const lemur::api::Exception& e) {
        PyErr_SetString(PyExc_IOError, e.what().c_str());

        Py_DECREF(query_bytes);

        return NULL;
    }

    Py_DECREF(query_bytes);

    std::vector<indri::api::ScoredExtentResult> query_results =
        query_annotation->getResults();

    std::vector<indri::api::ScoredExtentResult>::const_iterator it = query_results.begin();

    std::vector<string> snippets;

    if (include_snippets) {
        indri::api::SnippetBuilder builder(false /* html */);

        std::vector<lemur::api::DOCID_T> documentIDs(query_results.size(), 0);

        for (size_t i = 0; i < query_results.size(); ++i) {
            documentIDs[i] = query_results[i].document;
        }

        try {
            const std::vector<indri::api::ParsedDocument*> documents =
                self->query_env_->documents(documentIDs);

            for (size_t i = 0; i < query_results.size(); ++i) {
                snippets.push_back(
                    builder.build(documentIDs[i], documents[i], query_annotation));

                delete documents[i];
            }
        } catch (const lemur::api::Exception& e) {}
    }

    delete query_annotation;

    if (include_snippets && snippets.empty()) {
        PyErr_SetString(PyExc_IOError,
                        "Unable to retrieve snippets. "
                        "Make sure storeDocs is enabled "
                        "in your Indri configuration.");

        return NULL;
    }

    PyObject* results = PyTuple_New(query_results.size());

    Py_ssize_t pos = 0;
    for (; it != query_results.end(); ++it, ++pos) {
        PyObject* const result = PyTuple_New(include_snippets ? 3 : 2);

        PyTuple_SetItem(result, 0, PyLong_FromLong(it->document));
        PyTuple_SetItem(result, 1, PyFloat_FromDouble(it->score));

        if (include_snippets) {
            PyTuple_SetItem(result, 2, PyUnicode_Decode(snippets[pos].c_str(),
                                                        snippets[pos].size(),
                                                        ENCODING,
                                                        "strict"));
        }

       PyTuple_SetItem(results, pos, result);
    }

    return results;
}

static PyObject* Index_get_dictionary(Index* self, PyObject* args) {
    indri::index::VocabularyIterator* const vocabulary_it = self->index_->vocabularyIterator();

    PyObject* const token2id = PyDict_New();
    PyObject* const id2token = PyDict_New();
    PyObject* const id2df = PyDict_New();

    vocabulary_it->startIteration();

    while (!vocabulary_it->finished()) {
        indri::index::DiskTermData* const term_data = vocabulary_it->currentEntry();

        const lemur::api::TERMID_T term_id = term_data->termID;
        const string term = term_data->termData->term;

        const unsigned int document_frequency = term_data->termData->corpus.documentCount;
        CHECK_GT(document_frequency, 0);

        PyDict_SetItem(token2id,
                       PyUnicode_Decode(term.c_str(),
                                        term.size(),
                                        ENCODING,
                                        "strict"),
                       PyLong_FromLong(term_id));

        PyDict_SetItem(id2token,
                       PyLong_FromLong(term_id),
                       PyUnicode_Decode(term.c_str(),
                                        term.size(),
                                        ENCODING,
                                        "strict"));

        PyDict_SetItem(id2df,
                       PyLong_FromLong(term_id),
                       PyLong_FromLong(document_frequency));

        vocabulary_it->nextEntry();
    }

    delete vocabulary_it;

    CHECK_EQ(PyDict_Size(token2id), self->index_->uniqueTermCount());
    CHECK_EQ(PyDict_Size(id2token), self->index_->uniqueTermCount());
    CHECK_EQ(PyDict_Size(id2df), self->index_->uniqueTermCount());

    return PyTuple_Pack(3, token2id, id2token, id2df);
}

static PyObject* Index_get_term_frequencies(Index* self, PyObject* args) {
    indri::index::VocabularyIterator* const vocabulary_it = self->index_->vocabularyIterator();

    PyObject* const id2tf = PyDict_New();

    vocabulary_it->startIteration();

    while (!vocabulary_it->finished()) {
        indri::index::DiskTermData* const term_data = vocabulary_it->currentEntry();

        const lemur::api::TERMID_T term_id = term_data->termID;

        const uint64_t term_frequency = term_data->termData->corpus.totalCount;
        CHECK_GT(term_frequency, 0);

        PyDict_SetItem(id2tf,
                       PyLong_FromLong(term_id),
                       PyLong_FromLong(term_frequency));

        vocabulary_it->nextEntry();
    }

    delete vocabulary_it;

    CHECK_EQ(PyDict_Size(id2tf), self->index_->uniqueTermCount());

    return id2tf;
}

static PyMethodDef Index_methods[] = {
    {"document_ids", (PyCFunction) Index_get_document_ids, METH_VARARGS,
     "Returns the internal DOC_IDs given the external identifiers."},
    {"document", (PyCFunction) Index_document, METH_VARARGS,
     "Return a document (ext_document_id, terms) pair."},
    {"document_base", (PyCFunction) Index_document_base, METH_NOARGS,
     "Returns the lower bound document identifier (inclusive)."},
    {"maximum_document", (PyCFunction) Index_maximum_document, METH_NOARGS,
     "Returns the upper bound document identifier (exclusive)."},

    {"document_count", (PyCFunction) Index_document_count, METH_NOARGS,
     "Returns the number of documents in the index."},
    {"document_length", (PyCFunction) Index_document_length, METH_VARARGS,
     "Returns the length of a document."},

    {"total_terms", (PyCFunction) Index_total_terms, METH_NOARGS,
     "Returns the number of total terms in the index."},
    {"unique_terms", (PyCFunction) Index_unique_terms, METH_NOARGS,
     "Returns the number of unique terms in the index."},

    {"term_count", (PyCFunction) Index_term_count, METH_VARARGS,
     "Return the term frequency for a term."},

    {"query", (PyCFunction) Index_run_query, METH_VARARGS | METH_KEYWORDS,
     "Queries an Indri index."},

    {"get_dictionary", (PyCFunction) Index_get_dictionary, METH_NOARGS,
     "Extracts the dictionary from the index."},
    {"get_term_frequencies", (PyCFunction) Index_get_term_frequencies, METH_NOARGS,
     "Extracts the term frequencies from the index."},
    {NULL}  /* Sentinel */
};

static PyTypeObject IndexType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyndri.Index",             /* tp_name */
    sizeof(Index),             /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor) Index_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    "Index objects",           /* tp_doc */
    0,                   /* tp_traverse */
    0,                   /* tp_clear */
    0,                   /* tp_richcompare */
    0,                   /* tp_weaklistoffset */
    0,                   /* tp_iter */
    0,                   /* tp_iternext */
    Index_methods,             /* tp_methods */
    Index_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc) Index_init,      /* tp_init */
    0,                         /* tp_alloc */
    Index_new,                 /* tp_new */
};

// Module methods.

static PyObject* pyndri_stem(PyObject* self, PyObject* args) {
    PyObject* term;

    if (!PyArg_ParseTuple(args, "U", &term)) {
        return NULL;
    }

    CHECK(PyUnicode_Check(term));

    PyObject* term_bytes = PyUnicode_AsEncodedString(term, ENCODING, "strict");

    if (term_bytes == NULL) {
        return NULL;
    }

    char* term_str = PyBytes_AsString(term_bytes);

    static indri::parse::KrovetzStemmer stemmer;
    std::string stemmed_term(stemmer.kstem_stemmer(term_str));

    Py_DECREF(term_bytes);

    PyObject* result = PyUnicode_Decode(stemmed_term.c_str(),
                                        stemmed_term.size(),
                                        ENCODING,
                                        "strict");

    // Canonical; to indicate that result can be NULL.
    if (result == NULL) {
        return NULL;
    }

    return result;
}

static PyMethodDef PyndriMethods[] = {
    {"stem", (PyCFunction) pyndri_stem, METH_VARARGS,
     "Return the Krovetz stemmed version of a term."},
    {NULL, NULL, 0, NULL}
};

static PyModuleDef PyndriModule = {
    PyModuleDef_HEAD_INIT,
    "pyndri_ext",
    "Python interface to the Indri search engine.",
    -1,
    PyndriMethods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_pyndri_ext(void) {
    if (PyType_Ready(&IndexType) < 0) {
        return NULL;
    }

    PyObject* const module = PyModule_Create(&PyndriModule);

    if (module == NULL) {
        return NULL;
    }

    Py_INCREF(&IndexType);
    PyModule_AddObject(module, "Index", (PyObject*) &IndexType);

    return module;
}
