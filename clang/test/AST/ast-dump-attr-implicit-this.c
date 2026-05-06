// RUN: %clang_cc1 %s -ast-dump | FileCheck %s

// An attribute argument that names a sibling field by unqualified name in
// C is modelled as a MemberExpr whose base is an ImplicitThisExpr of
// pointer type to the enclosing record.

struct Mutex {};

// CHECK-LABEL: RecordDecl {{.+}} struct on_field definition
// CHECK:       FieldDecl {{.+}} data 'int'
// CHECK-NEXT:    GuardedByAttr
// CHECK-NEXT:      MemberExpr {{.+}} 'struct Mutex *' lvalue ->mu
// CHECK-NEXT:        ImplicitThisExpr {{.+}} 'struct on_field *'
struct on_field {
  struct Mutex *mu;
  int data __attribute__((guarded_by(mu)));
};

// CHECK-LABEL: RecordDecl {{.+}} struct on_anon_field definition
// CHECK:       FieldDecl {{.+}} data 'int'
// CHECK-NEXT:    GuardedByAttr
// CHECK:           ImplicitThisExpr {{.+}} 'struct on_anon_field *'
struct on_anon_field {
  struct {
    struct Mutex *mu;
  };
  int data __attribute__((guarded_by(mu)));
};
