#ifdef _WIN32
#include <ws2tcpip.h>
#endif
#include <errno.h>
#include <memory.h>
#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>

#if 1
#define ARENA_SAVE \
  int ai = mrb_gc_arena_save(mrb); \
  if (ai == MRB_GC_ARENA_SIZE) { \
    mrb_raise(mrb, E_RUNTIME_ERROR, "arena overflow"); \
  }
#define ARENA_RESTORE \
  mrb_gc_arena_restore(mrb, ai);
#else
#define ARENA_SAVE
#define ARENA_RESTORE
#endif

typedef struct {
  mrb_state *mrb;
  MYSQL *db;
} mrb_mysql_database;

typedef struct {
  mrb_state *mrb;
  mrb_value db;
  MYSQL_STMT *stmt;
  MYSQL_RES *res;
  MYSQL_FIELD *flds;
  MYSQL_BIND *bind;
} mrb_mysql_resultset;

static void
mrb_mysql_database_free(mrb_state *mrb, void *p) {
  mrb_mysql_database* db = (mrb_mysql_database*) p;
  if (db->db) {
    mysql_close(db->db);
  }
  free(db);
}

static void
mrb_mysql_resultset_free(mrb_state *mrb, void *p) {
  mrb_mysql_resultset* rs = (mrb_mysql_resultset*) p;
  if (rs->bind) {
    size_t i;
    for (i = 0; i < rs->res->field_count; i++)
      free(rs->bind[i].buffer);
  }
  if (rs->stmt) {
    mysql_stmt_free_result(rs->stmt);
    mysql_stmt_close(rs->stmt);
  }
  free(rs);
}

static const struct mrb_data_type mrb_mysql_database_type = {
  "mrb_mysql_database", mrb_mysql_database_free,
};

static const struct mrb_data_type mrb_mysql_resultset_type = {
  "mrb_mysql_resultset", mrb_mysql_resultset_free,
};

static mrb_value
mrb_mysql_database_init(mrb_state *mrb, mrb_value self) {
  mrb_value arg_host, arg_user, arg_passwd, arg_dbname;
  mrb_int arg_port = 0;
  mrb_value arg_sock = mrb_nil_value();
  mrb_int arg_flags = 0;
  MYSQL* mdb;
  mrb_mysql_database* db;

  mrb_get_args(mrb, "SSSS|i|S|i", &arg_host, &arg_user, &arg_passwd, &arg_dbname, &arg_port, &arg_sock, &arg_flags);

  mdb = mysql_init(NULL);
  if (!mysql_real_connect(
    mdb,
    RSTRING_PTR(arg_host),
    RSTRING_PTR(arg_user),
    RSTRING_PTR(arg_passwd),
    RSTRING_PTR(arg_dbname),
    arg_port,
    mrb_nil_p(arg_sock) ? NULL : RSTRING_PTR(arg_sock),
    arg_flags)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_error(mdb));
  }
  mysql_options(mdb, MYSQL_SET_CHARSET_NAME, "utf-8");

  db = (mrb_mysql_database*) malloc(sizeof(mrb_mysql_database));
  if (!db) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "can't memory alloc");
  }
  memset(db, 0, sizeof(mrb_mysql_database));
  db->mrb = mrb;
  db->db = mdb;
  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "context"), mrb_obj_value(
    Data_Wrap_Struct(mrb, mrb->object_class,
    &mrb_mysql_database_type, (void*) db)));
  return self;
}

static const char*
bind_params(mrb_state* mrb, MYSQL_STMT* stmt, int argc, mrb_value* argv, int columns, MYSQL_BIND* params) {
  int i;

  if (!columns) return NULL;
  memset(params, 0, columns * sizeof(MYSQL_BIND));
  for (i = 0; i < columns; i++) {
    switch (mrb_type(argv[i])) {
    case MRB_TT_UNDEF:
      params[i].buffer_type = MYSQL_TYPE_NULL;
      break;
    case MRB_TT_STRING:
      params[i].buffer_type = MYSQL_TYPE_STRING;
      params[i].buffer_length = RSTRING_LEN(argv[i]);
      params[i].buffer = RSTRING_PTR(argv[i]);
      break;
    case MRB_TT_FIXNUM:
      params[i].buffer_type = MYSQL_TYPE_LONG;
      params[i].buffer = &mrb_fixnum(argv[i]);
      break;
    case MRB_TT_FLOAT:
      params[i].buffer_type = MYSQL_TYPE_FLOAT;
      params[i].buffer = &mrb_float(argv[i]);
      break;
    case MRB_TT_TRUE:
      params[i].buffer_type = MYSQL_TYPE_TINY;
      params[i].buffer = &mrb_fixnum(argv[i]);
      break;
    case MRB_TT_FALSE:
      params[i].buffer_type = MYSQL_TYPE_TINY;
      params[i].buffer = &mrb_fixnum(argv[i]);
      break;
    default:
      return "invalid argument";
      break;
    }
  }
  return mysql_stmt_bind_param(stmt, params) == 0 ?
    NULL : mysql_stmt_error(stmt);
}

static void
bind_to_cols(mrb_state* mrb, mrb_value cols, MYSQL_RES* res, MYSQL_FIELD* flds, MYSQL_BIND* results) {
  size_t i;
  ARENA_SAVE;
  mrb_ary_clear(mrb, cols);
  for (i = 0; i < res->field_count; i++) {
    if (results[i].is_null_value) {
      mrb_ary_push(mrb, cols, mrb_nil_value());
    } else {
      switch (flds[i].type) {
      case MYSQL_TYPE_DECIMAL:
        mrb_ary_push(mrb, cols, mrb_fixnum_value(*(int*)results[i].buffer));
        break;
      case MYSQL_TYPE_LONG:
        mrb_ary_push(mrb, cols, mrb_fixnum_value(*(long*)results[i].buffer));
        break;
    	case MYSQL_TYPE_TINY:
        mrb_ary_push(mrb, cols, mrb_fixnum_value(*(char*)results[i].buffer));
        break;
    	case MYSQL_TYPE_SHORT:
        mrb_ary_push(mrb, cols, mrb_fixnum_value(*(short*)results[i].buffer));
        break;
    	case MYSQL_TYPE_LONGLONG:
        mrb_ary_push(mrb, cols, mrb_fixnum_value((mrb_int) *(long long int*)results[i].buffer));
        break;
    	case MYSQL_TYPE_FLOAT:
        mrb_ary_push(mrb, cols, mrb_float_value(mrb, *(float*)results[i].buffer));
        break;
    	case MYSQL_TYPE_DOUBLE:
        mrb_ary_push(mrb, cols, mrb_float_value(mrb, *(double*)results[i].buffer));
        break;
    	case MYSQL_TYPE_BLOB:
        mrb_ary_push(mrb, cols, mrb_str_new(mrb, results[i].buffer, results[i].length_value));
        break;
    	case MYSQL_TYPE_STRING:
        mrb_ary_push(mrb, cols, mrb_str_new_cstr(mrb, results[i].buffer));
        break;
    	case MYSQL_TYPE_NULL:
        mrb_ary_push(mrb, cols, mrb_nil_value());
        break;
      default:
        mrb_ary_push(mrb, cols, mrb_fixnum_value(*(long*)results[i].buffer));
        break;
      }
    }
    ARENA_RESTORE;
  }
}

static mrb_value
mrb_mysql_database_execute(mrb_state *mrb, mrb_value self) {
  int argc = 0;
  mrb_value* argv = NULL;
  mrb_value b = mrb_nil_value();
  mrb_value value_context;
  mrb_mysql_database* db = NULL;
  mrb_value query;
  char* sql;
  int len;
  MYSQL_STMT* stmt;
  MYSQL_BIND* params = NULL;
  MYSQL_RES* res;
  MYSQL_FIELD* flds;
  mrb_value fields;
  MYSQL_BIND* results;
  size_t i;
  mrb_value cols;
  mrb_value args[2];

  mrb_get_args(mrb, "&*", &b, &argv, &argc);

  if (argc == 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  query = argv[0];
  sql = RSTRING_PTR(query);
  len = RSTRING_LEN(query);

  stmt = mysql_stmt_init(db->db);
  if (mysql_stmt_prepare(stmt, sql, len) > 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_error(db->db));
  }

  if (argc > 1) {
    const char* error;
    int columns = mysql_stmt_param_count(stmt);
    params = (MYSQL_BIND*) malloc(columns * sizeof(MYSQL_BIND));
    if (!params) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "can't memory alloc");
    }
    error = bind_params(mrb, stmt, argc-1, &argv[1], columns, params);
    if (error) {
      free(params);
      mysql_stmt_close(stmt);
      mrb_raise(mrb, E_ARGUMENT_ERROR, error);
    }
  }

  if (mysql_stmt_execute(stmt) != 0) {
    mysql_stmt_close(stmt);
    mrb_raise(mrb, E_ARGUMENT_ERROR, mysql_stmt_error(stmt));
  }

  if (params)
    free(params);

  res = mysql_stmt_result_metadata(stmt);
  if (!res) {
    return mrb_nil_value();
  }
  flds = mysql_fetch_field(res);
  fields = mrb_ary_new(mrb);
  for (i = 0; i < res->field_count; i++) {
    const char* name = flds[i].name;
    mrb_ary_push(mrb, fields, mrb_str_new_cstr(mrb, name));
  }

  results = (MYSQL_BIND*) malloc(res->field_count * sizeof(MYSQL_BIND));
  memset(results, 0, res->field_count * sizeof(MYSQL_BIND));
  for (i = 0; i < res->field_count; i++) {
    results[i].buffer_type = flds[i].type;
    if (flds[i].type == MYSQL_TYPE_STRING) {
      results[i].buffer = malloc(flds[i].length);
      results[i].buffer_length = flds[i].length;
    } else {
      results[i].buffer = malloc(sizeof(long long int));
      results[i].buffer_length = sizeof(long long int);
    }
    results[i].length = &results[i].length_value;
    results[i].is_null = &results[i].is_null_value;
  }
  if (mysql_stmt_bind_result(stmt, results) != 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_stmt_error(stmt));
  }

  if (mrb_nil_p(b)) {
    struct RClass* _class_mysql;
    struct RClass* _class_mysql_resultset;
    mrb_value c;
    mrb_mysql_resultset* rs = (mrb_mysql_resultset*)
      malloc(sizeof(mrb_mysql_resultset));
    if (!rs) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "can't memory alloc");
    }
    memset(rs, 0, sizeof(mrb_mysql_resultset));
    rs->mrb = mrb;
    rs->stmt = stmt;
    mysql_use_result(db->db);
    rs->res = res;
    rs->flds = flds;
    rs->bind = results;

    _class_mysql = mrb_class_get(mrb, "MySQL");
    _class_mysql_resultset = mrb_class_ptr(mrb_const_get(mrb, mrb_obj_value(_class_mysql), mrb_intern_lit(mrb, "ResultSet")));
    c = mrb_class_new_instance(mrb, 0, NULL, _class_mysql_resultset);
    mrb_iv_set(mrb, c, mrb_intern_lit(mrb, "context"), mrb_obj_value(
      Data_Wrap_Struct(mrb, mrb->object_class,
      &mrb_mysql_resultset_type, (void*) rs)));
    mrb_iv_set(mrb, c, mrb_intern_lit(mrb, "fields"), fields);
    mrb_iv_set(mrb, c, mrb_intern_lit(mrb, "db"), self);
    mrb_iv_set(mrb, c, mrb_intern_lit(mrb, "eof"), mrb_false_value());
    return c;
  }

  cols = mrb_ary_new(mrb);
  args[0] = cols;
  args[1] = fields;
  while (mysql_stmt_fetch(stmt) == 0) {
    ARENA_SAVE;
    bind_to_cols(mrb, cols, res, flds, results);
    mrb_yield_argv(mrb, b, 2, args);
    ARENA_RESTORE;
  }
  for (i = 0; i < res->field_count; i++) {
    free(results[i].buffer);
  }
  free(results);
  if (mysql_stmt_free_result(stmt) != 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_stmt_error(stmt));
  }
  mysql_free_result(res);
  mysql_stmt_close(stmt);
  return mrb_nil_value();
}

static mrb_value
mrb_mysql_database_execute_batch(mrb_state *mrb, mrb_value self) {
  int argc = 0;
  mrb_value *argv;
  mrb_value value_context;
  mrb_mysql_database* db = NULL;
  mrb_value query;
  char* sql;
  int len;
  MYSQL_STMT* stmt;
  MYSQL_BIND* params = NULL;

  mrb_get_args(mrb, "*", &argv, &argc);

  if (argc == 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }

  query = argv[0];
  sql = RSTRING_PTR(query);
  len = RSTRING_LEN(query);

  stmt = mysql_stmt_init(db->db);
  if (mysql_stmt_prepare(stmt, sql, len) > 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_error(db->db));
  }

  if (argc > 1) {
    const char* error;
    int columns = mysql_stmt_param_count(stmt);
    params = (MYSQL_BIND*) malloc(columns * sizeof(MYSQL_BIND));
    if (!params) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "can't memory alloc");
    }
    error = bind_params(mrb, stmt, argc-1, &argv[1], columns, params);
    if (error) {
      free(params);
      mysql_stmt_close(stmt);
      mrb_raise(mrb, E_ARGUMENT_ERROR, error);
    }
  }

  if (mysql_stmt_execute(stmt) != 0) {
    mysql_stmt_close(stmt);
    mrb_raise(mrb, E_ARGUMENT_ERROR, mysql_stmt_error(stmt));
  }
  if (params)
    free(params);

  mysql_stmt_close(stmt);
  return mrb_fixnum_value(0);
}

static mrb_value
mrb_mysql_database_close(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_database* db = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (db->db) {
    mysql_close(db->db);
    db->db = NULL;
  }
  return mrb_nil_value();
}

static mrb_value
mrb_mysql_database_last_insert_rowid(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_database* db = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  return mrb_fixnum_value((mrb_int) mysql_insert_id(db->db));
}

static mrb_value
mrb_mysql_database_changes(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_database* db = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  return mrb_fixnum_value((mrb_int) mysql_affected_rows(db->db));
}

/*
static mrb_value
mrb_mysql_database_exec(mrb_state *mrb, mrb_value self, const char* query) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_database* db = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  int r = mysql_query(db->db, query);
  if (r != 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_error(db->db));
  }
  return mrb_nil_value();
}
*/

static mrb_value
mrb_mysql_database_transaction(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_database* db = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (mysql_autocommit(db->db, 0) != 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_error(db->db));
  }
  return mrb_nil_value();
}

static mrb_value
mrb_mysql_database_commit(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_database* db = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (mysql_commit(db->db) != 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_error(db->db));
  }
  return mrb_nil_value();
}

static mrb_value
mrb_mysql_database_rollback(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_database* db = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_database_type, db);
  if (!db) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (mysql_rollback(db->db) != 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_error(db->db));
  }
  return mrb_nil_value();
}

static mrb_value
mrb_mysql_resultset_next(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_resultset* rs = NULL;
  mrb_value cols;
  int ai;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_resultset_type, rs);
  if (!rs) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (mrb_type(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "eof"))) == MRB_TT_TRUE) {
    return mrb_nil_value();
  }
  if (mysql_stmt_fetch(rs->stmt) != 0) {
    if (mysql_stmt_errno(rs->stmt) != 0) {
      mrb_raise(mrb, E_RUNTIME_ERROR, mysql_stmt_error(rs->stmt));
    }
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "eof"), mrb_true_value());
    return mrb_nil_value();
  }
  ai = mrb_gc_arena_save(mrb);
  cols = mrb_ary_new(mrb);
  bind_to_cols(mrb, cols, rs->res, rs->flds, rs->bind);
  mrb_gc_arena_restore(mrb, ai);
  return cols;
}

static mrb_value
mrb_mysql_resultset_close(mrb_state *mrb, mrb_value self) {
  mrb_value value_context = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "context"));
  mrb_mysql_resultset* rs = NULL;
  Data_Get_Struct(mrb, value_context, &mrb_mysql_resultset_type, rs);
  if (!rs) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  if (mysql_stmt_close(rs->stmt) != 0) {
    mrb_raise(mrb, E_RUNTIME_ERROR, mysql_stmt_error(rs->stmt));
  }
  rs->stmt = NULL;
  return mrb_nil_value();
}

static mrb_value
mrb_mysql_resultset_fields(mrb_state *mrb, mrb_value self) {
  return mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "fields"));
}

static mrb_value
mrb_mysql_resultset_eof(mrb_state *mrb, mrb_value self) {
  return mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "eof"));
}

void
mrb_mruby_mysql_gem_init(mrb_state* mrb) {
  struct RClass *_class_mysql;
  struct RClass *_class_mysql_database;
  struct RClass* _class_mysql_resultset;
  ARENA_SAVE;

  _class_mysql = mrb_define_module(mrb, "MySQL");

  _class_mysql_database = mrb_define_class_under(mrb, _class_mysql, "Database", mrb->object_class);
  mrb_define_method(mrb, _class_mysql_database, "initialize", mrb_mysql_database_init, ARGS_OPT(1));
  mrb_define_method(mrb, _class_mysql_database, "execute", mrb_mysql_database_execute, ARGS_ANY());
  mrb_define_method(mrb, _class_mysql_database, "execute_batch", mrb_mysql_database_execute_batch, ARGS_ANY());
  mrb_define_method(mrb, _class_mysql_database, "close", mrb_mysql_database_close, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_database, "last_insert_rowid", mrb_mysql_database_last_insert_rowid, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_database, "changes", mrb_mysql_database_changes, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_database, "transaction", mrb_mysql_database_transaction, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_database, "commit", mrb_mysql_database_commit, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_database, "rollback", mrb_mysql_database_rollback, ARGS_NONE());
  ARENA_RESTORE;

  _class_mysql_resultset = mrb_define_class_under(mrb, _class_mysql, "ResultSet", mrb->object_class);
  mrb_define_method(mrb, _class_mysql_resultset, "next", mrb_mysql_resultset_next, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_resultset, "close", mrb_mysql_resultset_close, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_resultset, "fields", mrb_mysql_resultset_fields, ARGS_NONE());
  mrb_define_method(mrb, _class_mysql_resultset, "eof?", mrb_mysql_resultset_eof, ARGS_NONE());
  ARENA_RESTORE;
}

void
mrb_mruby_mysql_gem_final(mrb_state* mrb) {
}

/* vim:set et ts=2 sts=2 sw=2 tw=0: */
