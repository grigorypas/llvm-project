// RUN: %clang_cc1 -std=c++2a -Wno-unused-value %s -verify
// RUN: %clang_cc1 -std=c++2b -Wno-unused-value %s -verify

consteval int id(int i) { return i; }
constexpr char id(char c) { return c; }

template <typename T>
constexpr int f(T t) { // expected-note {{declared here}}
    return t + id(t);  // expected-note 2{{'f<int>' is an immediate function because its body contains a call to a consteval function 'id' and that call is not a constant expression}}
}

namespace examples {

auto a = &f<char>; // ok, f<char> is not an immediate function
auto b = &f<int>;  // expected-error {{cannot take address of immediate function 'f<int>' outside of an immediate invocation}}

static_assert(f(3) == 6); // ok

template <typename T>
constexpr int g(T t) {    // g<int> is not an immediate function
    return t + id(42);    // because id(42) is already a constant
}

template <typename T, typename F>
constexpr bool is_not(T t, F f) {
    return not f(t);
}

consteval bool is_even(int i) { return i % 2 == 0; }

static_assert(is_not(5, is_even));

int x = 0; // expected-note {{declared here}}

template <typename T>
constexpr T h(T t = id(x)) { // expected-note {{read of non-const variable 'x' is not allowed in a constant expression}} \
                             // expected-note {{'hh<int>' is an immediate function because its body contains a call to a consteval function 'id' and that call is not a constant expression}}
    return t;
}

template <typename T>
constexpr T hh() {           // hh<int> is an immediate function
    [[maybe_unused]] auto x = h<T>();
    return h<T>();
}

int i = hh<int>(); // expected-error {{call to immediate function 'examples::hh<int>' is not a constant expression}} \
                   // expected-note {{in call to 'hh<int>()'}}

struct A {
  int x;
  int y = id(x);
};

template <typename T>
constexpr int k(int) {
  return A(42).y;
}

}

namespace nested {

template <typename T>
constexpr int fdupe(T t) {
    return id(t);
}

struct a {
  constexpr a(int) { }
};

a aa(fdupe<int>((f<int>(7))));

template <typename T>
constexpr int foo(T t);     // expected-note {{declared here}}

a bb(f<int>(foo<int>(7))); // expected-error{{call to immediate function 'f<int>' is not a constant expression}} \
                           // expected-note{{undefined function 'foo<int>' cannot be used in a constant expression}}

}

namespace e2{
template <typename T>
constexpr int f(T t);
auto a = &f<char>;
auto b = &f<int>;
}

namespace forward_declare_constexpr{
template <typename T>
constexpr int f(T t);

auto a = &f<char>;
auto b = &f<int>;

template <typename T>
constexpr int f(T t) {
    return id(0);
}
}

namespace forward_declare_consteval{
template <typename T>
constexpr int f(T t);

auto a = &f<char>;
auto b = &f<int>; // expected-error {{immediate function 'f<int>' used before it is defined}} \
                  // expected-note {{in instantiation of function template specialization}}

template <typename T>
constexpr int f(T t) { // expected-note {{'f<int>' defined here}}
    return id(t); // expected-note {{'f<int>' is an immediate function because its body contains a call to a consteval function 'id' and that call is not a constant expression}}
}
}

namespace constructors {
consteval int f(int) {
  return 0;
}
struct S {
  constexpr S(auto i) {
    f(i);
  }
};
constexpr void g(auto i) {
  [[maybe_unused]] S s{i};
}
void test() {
  g(0);
}
}

namespace aggregate {
consteval int f(int);
struct S{
    int a = 0;
    int b = f(a);
};

constexpr bool test(auto i) {
    S s{i};
    return s.b == 2 *i;
}
consteval int f(int i) {
    return 2 * i;
}

void test() {
    static_assert(test(42));
}

}

namespace ConstevalConstructor{
int x = 0; // expected-note {{declared here}}
struct S {
    consteval S(int) {};
};
constexpr int g(auto t) {
    S s(t); // expected-note {{'g<int>' is an immediate function because its body contains a call to a consteval constructor 'S' and that call is not a constant expression}}
    return 0;
}
int i = g(x); // expected-error {{call to immediate function 'ConstevalConstructor::g<int>' is not a constant expression}} \
              // expected-note {{read of non-const variable 'x' is not allowed in a constant expression}}
}



namespace Aggregate {
consteval int f(int); // expected-note {{declared here}}
struct S {
  int x = f(42); // expected-note {{undefined function 'f' cannot be used in a constant expression}} \
                 // expected-note {{'immediate<int>' is an immediate function because its body contains a call to a consteval function 'f' and that call is not a constant expression}}
};

constexpr S immediate(auto) {
    return S{};
}

void test_runtime() {
    (void)immediate(0); // expected-error {{call to immediate function 'Aggregate::immediate<int>' is not a constant expression}} \
                        // expected-note {{in call to 'immediate<int>(0)'}}
}
consteval int f(int i) {
    return i;
}
consteval void test() {
    constexpr S s = immediate(0);
    static_assert(s.x == 42);
}
}



namespace GH63742 {
void side_effect(); // expected-note  {{declared here}}
consteval int f(int x) {
    if (!x) side_effect(); // expected-note {{non-constexpr function 'side_effect' cannot be used in a constant expression}}
    return x;
}
struct SS {
  int y = f(1); // Ok
  int x = f(0); // expected-error {{call to consteval function 'GH63742::f' is not a constant expression}} \
                // expected-note  {{declared here}} \
                // expected-note  {{in call to 'f(0)'}}
  SS();
};
SS::SS(){} // expected-note {{in the default initializer of 'x'}}

consteval int f2(int x) {
    if (!__builtin_is_constant_evaluated()) side_effect();
    return x;
}
struct S2 {
    int x = f2(0);
    constexpr S2();
};

constexpr S2::S2(){}
S2 s = {};
constinit S2 s2 = {};

struct S3 {
    int x = f2(0);
    S3();
};
S3::S3(){}

}

namespace Defaulted {
consteval int f(int x);
struct SS {
  int x = f(0);
  SS() = default;
};
}

namespace DefaultedUse{
consteval int f(int x);  // expected-note {{declared here}}
struct SS {
  int a = sizeof(f(0)); // Ok
  int x = f(0); // expected-note {{undefined function 'f' cannot be used in a constant expression}}

  SS() = default; // expected-note {{'SS' is an immediate constructor because the default initializer of 'x' contains a call to a consteval function 'f' and that call is not a constant expression}}
};

void test() {
    [[maybe_unused]] SS s; // expected-error {{call to immediate function 'DefaultedUse::SS::SS' is not a constant expression}} \
                           //  expected-note {{in call to 'SS()'}}
}
}

namespace UserDefinedConstructors {
consteval int f(int x) {
    return x;
}
extern int NonConst; // expected-note 2{{declared here}}

struct ConstevalCtr {
    int y;
    int x = f(y);
    consteval ConstevalCtr(int yy)
    : y(f(yy)) {}
};

ConstevalCtr c1(1);
ConstevalCtr c2(NonConst);
// expected-error@-1 {{call to consteval function 'UserDefinedConstructors::ConstevalCtr::ConstevalCtr' is not a constant expression}} \
// expected-note@-1 {{read of non-const variable 'NonConst' is not allowed in a constant expression}}

struct ImmediateEscalating {
    int y;
    int x = f(y);
    template<typename T>
    constexpr ImmediateEscalating(T yy) // expected-note {{ImmediateEscalating<int>' is an immediate constructor because the initializer of 'y' contains a call to a consteval function 'f' and that call is not a constant expression}}
    : y(f(yy)) {}
};

ImmediateEscalating c3(1);
ImmediateEscalating c4(NonConst);
// expected-error@-1 {{call to immediate function 'UserDefinedConstructors::ImmediateEscalating::ImmediateEscalating<int>' is not a constant expression}} \
// expected-note@-1 {{read of non-const variable 'NonConst' is not allowed in a constant expression}}


struct NonEscalating {
    int y;
    int x = f(this->y); // expected-error {{call to consteval function 'UserDefinedConstructors::f' is not a constant expression}} \
                        // expected-note  {{declared here}} \
                        // expected-note  {{use of 'this' pointer is only allowed within the evaluation of a call to a 'constexpr' member function}}
    constexpr NonEscalating(int yy) : y(yy) {} // expected-note {{in the default initializer of 'x'}}
};
NonEscalating s = {1};

}

namespace AggregateInit {

consteval int f(int x) {
    return x;
}

struct S {
    int i;
    int j = f(i);
};

constexpr S  test(auto) {
    return {};
}

S s = test(0);

}

namespace GlobalAggregateInit {

consteval int f(int x) {
    return x;
}

struct S {
    int i;
    int j = f(i); // expected-error {{call to consteval function 'GlobalAggregateInit::f' is not a constant expression}} \
                  // expected-note {{implicit use of 'this' pointer is only allowed within the evaluation of a call to a 'constexpr' member function}} \
                  // expected-note {{declared here}}
};

S s(0); // expected-note {{in the default initializer of 'j'}}

}

namespace GH65985 {
consteval int invalid(); // expected-note 2{{declared here}}
constexpr int escalating(auto) {
    return invalid();
    // expected-note@-1 {{'escalating<int>' is an immediate function because its body contains a call to a consteval function 'invalid' and that call is not a constant expression}}
    // expected-note@-2 2{{undefined function 'invalid' cannot be used in a constant expression}}
}
struct S {
    static constexpr int a = escalating(0); // expected-note 2{{in call to}}
    // expected-error@-1 {{call to immediate function 'GH65985::escalating<int>' is not a constant expression}}
    // expected-error@-2 {{constexpr variable 'a' must be initialized by a constant expression}}
};

}

namespace GH66324 {

consteval int allocate();  // expected-note  2{{declared here}}

struct _Vector_base {
  int b =  allocate(); // expected-note 2{{undefined function 'allocate' cannot be used in a constant expression}} \
  // expected-error {{call to consteval function 'GH66324::allocate' is not a constant expression}} \
  // expected-note  {{declared here}}
};

template <typename>
struct vector : _Vector_base {
  constexpr vector()
  // expected-note@-1 {{'vector' is an immediate constructor because its body contains a call to a consteval function 'allocate' and that call is not a constant expression}}
  : _Vector_base{} {} // expected-note {{in the default initializer of 'b'}}
};

vector<void> v{};
// expected-error@-1 {{call to immediate function 'GH66324::vector<void>::vector' is not a constant expression}}
// expected-note@-2 {{in call to 'vector()'}}

}


namespace GH82258 {

template <class R, class Pred>
constexpr auto none_of(R&& r, Pred pred) -> bool { return true; }

struct info { int value; };
consteval auto is_invalid(info i) -> bool { return false; }
constexpr info types[] = { {1}, {3}, {5}};

static_assert(none_of(
    types,
    +[](info i) consteval {
        return is_invalid(i);
    }
));

static_assert(none_of(
    types,
    []{
        return is_invalid;
    }()
));

}

#if __cplusplus >= 202302L
namespace lvalue_to_rvalue_init_from_heap {

struct S {
    int *value;
    constexpr S(int v) : value(new int {v}) {}  // expected-note 2 {{heap allocation performed here}}
    constexpr ~S() { delete value; }
};
consteval S fn() { return S(5); }
int fn2() { return 2; }  // expected-note {{declared here}}

constexpr int a = *fn().value;
constinit int b = *fn().value;
const int c = *fn().value;
int d = *fn().value;

constexpr int e = *fn().value + fn2(); // expected-error {{must be initialized by a constant expression}} \
                                       // expected-error {{call to consteval function 'lvalue_to_rvalue_init_from_heap::fn' is not a constant expression}} \
                                       // expected-note {{non-constexpr function 'fn2'}} \
                                       // expected-note {{pointer to heap-allocated object}}

int f = *fn().value + fn2();  // expected-error {{call to consteval function 'lvalue_to_rvalue_init_from_heap::fn' is not a constant expression}} \
                              // expected-note {{pointer to heap-allocated object}}
}
#endif


#if __cplusplus >= 202302L

namespace GH91509 {

consteval int f(int) { return 0; }

template<typename T>
constexpr int g(int x) {
    if consteval {
        return f(x);
    }
    if !consteval {}
    else {
        return f(x);
    }
    return 1;
}

int h(int x) {
    return g<void>(x);
}
}

#endif


namespace GH91308 {
    constexpr void f(auto) {
        static_assert(false);
    }
    using R1 = decltype(&f<int>);
}

namespace GH94935 {

consteval void f(int) {}
consteval void undef(int); // expected-note {{declared here}}

template<typename T>
struct G {
    void g() {
        GH94935::f(T::fn());
        GH94935::f(T::undef2());  // expected-error {{call to consteval function 'GH94935::f' is not a constant expression}} \
                                  // expected-note  {{undefined function 'undef2' cannot be used in a constant expression}}
        GH94935::undef(T::fn());  // expected-error {{call to consteval function 'GH94935::undef' is not a constant expression}} \
                                  // expected-note  {{undefined function 'undef' cannot be used in a constant expression}}
    }
};

struct X {
    static consteval int fn() { return 0; }
    static consteval int undef2();  // expected-note {{declared here}}

};

void test() {
    G<X>{}.g(); // expected-note {{instantiation}}
}


template<typename T>
void g() {
    auto l = []{
        ::f(T::fn());
    };
}

struct Y {
    static int fn();
};

template void g<Y>();

}

namespace GH112677 {

class ConstEval {
 public:
  consteval ConstEval(int); // expected-note 2{{declared here}}
};

struct TemplateCtor {
    ConstEval val;
    template <class Anything = int> constexpr
    TemplateCtor(int arg) : val(arg) {} // expected-note {{undefined constructor 'ConstEval'}}
};
struct C : TemplateCtor {
    using TemplateCtor::TemplateCtor; // expected-note {{in call to 'TemplateCtor<int>(0)'}}
};

C c(0); // expected-note{{in implicit initialization for inherited constructor of 'C'}}
// expected-error@-1 {{call to immediate function 'GH112677::C::TemplateCtor' is not a constant expression}}

struct SimpleCtor { constexpr SimpleCtor(int) {}};
struct D : SimpleCtor {
    int y = 10;
    ConstEval x = y; // expected-note {{undefined constructor 'ConstEval'}}
    using SimpleCtor::SimpleCtor;
    //expected-note@-1 {{'SimpleCtor' is an immediate constructor because the default initializer of 'x' contains a call to a consteval constructor 'ConstEval' and that call is not a constant expression}}
};

D d(0); // expected-note {{in implicit initialization for inherited constructor of 'D'}}
// expected-error@-1 {{call to immediate function 'GH112677::D::SimpleCtor' is not a constant expression}}

}

namespace GH123405 {

consteval void fn() {}

template <typename>
constexpr auto tfn(int) {
    auto p = &fn;  // expected-note {{'tfn<int>' is an immediate function because its body evaluates the address of a consteval function 'fn'}}
    return p;
}

void g() {
   int a; // expected-note {{declared here}}
   tfn<int>(a); // expected-error {{call to immediate function 'GH123405::tfn<int>' is not a constant expression}}\
                // expected-note {{read of non-const variable 'a' is not allowed in a constant expression}}
}
} // namespace GH123405

namespace GH118000 {
consteval int baz() { return 0;}
struct S {
    int mSize = baz();
};

consteval void bar() {
    S s;
}

void foo() {
    S s;
}
} // namespace GH118000

namespace GH119046 {

template <typename Cls> constexpr auto tfn(int) {
  return (unsigned long long)(&Cls::sfn);
  //expected-note@-1 {{'tfn<GH119046::S>' is an immediate function because its body evaluates the address of a consteval function 'sfn'}}
};
struct S { static consteval void sfn() {} };

int f() {
  int a = 0; // expected-note{{declared here}}
  return tfn<S>(a);
  //expected-error@-1 {{call to immediate function 'GH119046::tfn<GH119046::S>' is not a constant expression}}
  //expected-note@-2 {{read of non-const variable 'a' is not allowed in a constant expression}}
}
}

#if __cplusplus >= 202302L
namespace GH135281 {
  struct B {
    const void* p;
    consteval B() : p{this} {}
  };
  B b;
  B b2{};
  B &&b3{};
  void f() {
    static B b4;
    B b5; // expected-error {{call to consteval function 'GH135281::B::B' is not a constant expression}} \
          // expected-note {{pointer to temporary is not a constant expression}} \
          // expected-note {{temporary created here}}
  }
  template<typename T> T temp_var_uninit;
  template<typename T> T temp_var_brace_init{};
  B* b6 = &temp_var_uninit<B>;
  B* b7 = &temp_var_brace_init<B>;
  B* b8 = &temp_var_brace_init<B&&>;
  template<typename T> void f2() {
    static T b9;
    T b10; // expected-error {{call to consteval function 'GH135281::B::B' is not a constant expression}} \
           // expected-note {{pointer to temporary is not a constant expression}} \
           // expected-note {{temporary created here}}
    static B b11;
    B b12; // expected-error 2 {{call to consteval function 'GH135281::B::B' is not a constant expression}} \
           // expected-note 2 {{pointer to temporary is not a constant expression}} \
           // expected-note 2 {{temporary created here}}
  }
  void (*ff)() = f2<B>; // expected-note {{instantiation of function template specialization}}
}
#endif
