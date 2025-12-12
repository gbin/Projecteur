// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <QMetaObject>
#include <QPointer>

#if (QT_VERSION < QT_VERSION_CHECK(5, 10, 0))
#include <QCoreApplication>
#include <QEvent>
#endif

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace async {

// capture_call helper method and apply for C++14 taken from here:
// https://stackoverflow.com/a/49902823

// Implementation detail of a simplified std::apply from C++17
template <typename F, typename Tuple, std::size_t ... I>
constexpr decltype(auto)
apply_impl(F&& f, Tuple&& t, std::index_sequence<I ...>){
  return static_cast<F&&>(f)(std::get<I>(static_cast<Tuple&&>(t)) ...);
}

// Implementation of a simplified std::apply from C++17
template <typename F, typename Tuple>
constexpr decltype(auto) apply(F&& f, Tuple&& t){
  return apply_impl(
    static_cast<F&&>(f), static_cast<Tuple&&>(t),
    std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
}

// Capture args and add them as additional arguments
template <typename Lambda, typename ... Args>
auto capture_call(Lambda&& lambda, Args&& ... args){
  return [
    lambda = std::forward<Lambda>(lambda),
    capture_args = std::make_tuple(std::forward<Args>(args) ...)
  ](auto&& ... original_args)mutable{
    return async::apply([&lambda](auto&& ... args){

      // Handle the Qt6 return argument placeholder conditionally inside the body.
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
      // Check if the tuple size is greater than the expected arguments (i.e., Qt6 inserted the return placeholder).
      if constexpr (std::tuple_size<decltype(std::make_tuple(std::forward<decltype(args)>(args)...))>::value > sizeof...(Args))
      {
        // Unpack the arguments, skip the first one (the return value placeholder), and call the user-provided lambda.
        auto tuple = std::make_tuple(std::forward<decltype(args)>(args)...);

        // std::apply applies a tuple to a function. We use a nested lambda here to skip the first element.
        std::apply([&lambda](auto&& /* retArg */, auto&& ... actual_args){
          lambda(std::forward<decltype(actual_args)>(actual_args)...);
        }, std::move(tuple));
      } else
#endif
      {
        // Qt5 or Qt6 without the return placeholder: pass arguments directly to the user-provided lambda.
        lambda(std::forward<decltype(args)>(args) ...);
      }
    },
    std::tuple_cat(
      std::forward_as_tuple(original_args ...),
      async::apply([](auto&& ... args){
        return std::forward_as_tuple<Args ...>(
          std::move(args) ...);
      }, std::move(capture_args))
    ));
  };
}

#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
// Invoke a (lambda) function for context QObject with queued connection.
template <typename F>
void invoke(QObject* context, F&& function) {
  QMetaObject::invokeMethod(context, std::forward<F>(function), Qt::QueuedConnection);
}
#else
// ... older Qt versions < 5.10
namespace detail {
template <typename F>
struct FEvent : public QEvent {
  using Fun = typename std::decay<F>::type;
  Fun fun;
  FEvent(Fun && fun) : QEvent(QEvent::None), fun(std::move(fun)) {}
  FEvent(const Fun & fun) : QEvent(QEvent::None), fun(fun) {}
  ~FEvent() { fun(); }
}; }

template <typename F>
void invoke(QObject* context, F&& function) {
  QCoreApplication::postEvent(context, new detail::FEvent<F>(std::forward<F>(function)));
}
#endif

// --- Helpers to deduce std::function type from a lambda.
template <typename>
struct remove_member;

template <typename C, typename T>
struct remove_member<T C::*> {
  using type = T;
};

template <typename C, typename R, typename... Args>
struct remove_member<R (C::*)(Args...) const> {
  using type = R(Args...);
};

/// Create a safe function object, guaranteed to be invoked in the context of
/// the given QObject context.
template <typename F, typename R, typename... Args>
auto makeSafeCallback_impl(QObject* context, F&& f, std::function<R(Args...)>, bool autoConnection)
{
  QPointer<QObject> ctxPtr(context);
  return [ctxPtr, autoConnection, f=std::forward<F>(f)](Args&&... args) mutable
  {
    // Check if context object is still valid
    if (ctxPtr.isNull()) {
      return;
    }

    #if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    QMetaObject::invokeMethod(ctxPtr,
      capture_call(std::forward<F>(f), std::forward<Args>(args)...),
      autoConnection ? Qt::AutoConnection : Qt::QueuedConnection);
    // Note: if forceQueued is false and current thread is the same as
    // the context thread -> execute directly
    #else
    // For Qt < 5.10 the call is always queued via the event queue
    async::invoke(ctxPtr, capture_call(std::forward<F>(f), std::forward<Args>(args)...));
    #endif
  };
}

/// Create a safe function object, guaranteed to be invoked in the context of
/// the given QObject context.
template <typename F>
auto makeSafeCallback(QObject* context, F&& f, bool autoConnection) {
  using sig = decltype(&F::operator());
  using ft = std::function<typename remove_member<sig>::type>;
  return async::makeSafeCallback_impl(context, std::forward<F>(f), ft{}, autoConnection);
}

/// Deriving from this class will makeSafeCallback and postSelf methods for QObject based
/// classes available.
///
/// Example:
/// @code
/// class MyClass : public QObject, public async::Async<MyClass> {
///     Q_OBJECT
///     // ... implementation..
/// }
/// @endcode
template <typename T>
class Async
{
protected:
  /// Returns a function object that is guaranteed to be invoked in the own thread context.
  template <typename F>
  auto makeSafeCallback(F&& f, bool autoConnection = true) {
    return async::makeSafeCallback(static_cast<T*>(this), std::forward<F>(f), autoConnection);
  }

  /// Post a function to the own event loop.
  template <typename F>
  void postSelf(F&& function) {
    async::invoke(static_cast<T*>(this), std::forward<F>(function));
  }

public:
  /// Post a task to the object's event loop.
  template <typename Task>
  void postTask(Task&& task) {
    postSelf(std::forward<Task>(task));
  }

  template<typename Task>
  static constexpr bool is_void_return_v = std::is_same<std::result_of_t<Task()>, void>::value;

  /// Post a task with no return value and provide a callback.
  template <typename Task, typename Callback>
  typename std::enable_if_t<is_void_return_v<Task>>
  postCallback(Task&& task, Callback&& callback)
  {
    postSelf(
      [task = std::forward<Task>(task), callback = std::forward<Callback>(callback)]() mutable
      {
        task();
        callback();
      }
    );
  }

  /// Post a task with return value and a callback that takes the return value
  /// as an argument.
  template <typename Task, typename Callback>
  typename std::enable_if_t<!is_void_return_v<Task>>
  postCallback(Task&& task, Callback&& callback)
  {
    postSelf(
      [task = std::forward<Task>(task), callback = std::forward<Callback>(callback)]() mutable
      {
        callback(task());
      }
    );
  }
};

} // end namespace async
