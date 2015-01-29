//
// Copyright (c) 2014 Krzysztof Jusiak (krzysztof at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_DI_CORE_INJECTOR_HPP
#define BOOST_DI_CORE_INJECTOR_HPP

#include <memory>
#include "boost/di/aux_/type_traits.hpp"
#include "boost/di/aux_/utility.hpp"
#include "boost/di/core/any_type.hpp"
#include "boost/di/core/binder.hpp"
#include "boost/di/core/dependency.hpp"
#include "boost/di/core/policy.hpp"
#include "boost/di/core/pool.hpp"
#include "boost/di/core/provider.hpp"
#include "boost/di/scopes/exposed.hpp"
#include "boost/di/type_traits/ctor_traits.hpp"
#include "boost/di/concepts/creatable.hpp"

namespace boost { namespace di { namespace core {

template<class T, class = void>
struct get_deps {
    using type = typename T::deps;
};

template<class T>
struct get_deps<T, std::enable_if_t<has_configure<T>{}>> {
    using type = typename aux::function_traits<
        decltype(&T::configure)
    >::result_type::deps;
};

template<
    class T
  , class = typename is_injector<T>::type
  , class = typename is_dependency<T>::type
> struct add_type_list;

template<class T, class TAny>
struct add_type_list<T, std::true_type, TAny> {
    using type = typename get_deps<T>::type;
};

template<class T>
struct add_type_list<T, std::false_type, std::true_type> {
    using type = aux::type_list<T>;
};

template<class T>
struct add_type_list<T, std::false_type, std::false_type> {
    using type = aux::type_list<dependency<scopes::exposed<>, T>>;
};

template<class... Ts>
using bindings_t = aux::join_t<typename add_type_list<Ts>::type...>;

template<class T>
decltype(auto) pass_arg(const T& arg, std::enable_if_t<!has_configure<T>{}>* = 0) noexcept {
    return arg;
}

template<class T>
decltype(auto) pass_arg(const T& arg, std::enable_if_t<has_configure<T>{}>* = 0) noexcept {
    return arg.configure();
}

template<class T, class TWrapper>
struct wrapper {
    inline operator T() noexcept {
        return wrapper_;
    }

    TWrapper wrapper_;
};

BOOST_DI_HAS_METHOD(call, call);

template<class TConfig, class... TDeps>
class injector : public pool<bindings_t<TDeps...>> {
    template<class...> friend struct provider;

    using pool_t = pool<bindings_t<TDeps...>>;

public:
    using deps = bindings_t<TDeps...>;

    // bind<...>, etc.  -> ignore
    // injector<...>    -> get all dependencies from the injector
    // dependency<...>  -> pass

    template<class... TArgs>
    explicit injector(const init&, const TArgs&... args) noexcept
        : pool_t{init{}, pool<aux::type_list<
              std::remove_reference_t<decltype(pass_arg(args))>...>
          >{pass_arg(args)...}}
    { }

    template<class TConfig_, class... TDeps_>
    explicit injector(const injector<TConfig_, TDeps_...>& injector) noexcept
        : pool_t{init{}, create_from_injector(injector, deps{})}
    { }

    template<class T BOOST_DI_REQUIRES(concepts::creatable<deps, TConfig, T>())>
    T create() const {
        return create_t<T>();
    }

    template<class TAction>
    void call(const TAction& action) {
        call_impl(action, deps{});
    }

    TConfig& config() noexcept {
        return config_;
    }

    template<class T, class TName = no_name>
    auto create_t() const {
        auto&& dependency = binder::resolve<T, TName>((injector*)this);
        using dependency_t = std::remove_reference_t<decltype(dependency)>;
        using expected_t = typename dependency_t::expected;
        using given_t = typename dependency_t::given;
        using ctor_t = typename type_traits::ctor_traits<given_t>::type;
        using provider_t = provider<expected_t, given_t, T, ctor_t, injector>;
        policy<pool_t>::template call<T, TName>(
            ((TConfig&)config_).policies(), dependency, ctor_t{}
        );
        using wrapper_t = decltype(dependency.template create<T>(provider_t{*this}));
        using type = std::conditional_t<
            std::is_reference<T>{} && has_is_ref<dependency_t>{}
          , T
          , std::remove_reference_t<T>
        >;
        return wrapper<type, wrapper_t>{dependency.template create<T>(provider_t{*this})};
    }

private:
    template<class TAction, class... Ts>
    void call_impl(const TAction& action, const aux::type_list<Ts...>&) {
        int _[]{0, (call_impl<Ts>(action), 0)...}; (void)_;
    }

    template<class, class T>
    auto create_impl(const aux::type<T>&) const {
        return create_t<T>();
    }

    template<class TParent, class... Ts>
    auto create_impl(const aux::type<any_type<Ts...>>&) const {
        return any_type<TParent, injector>{*this};
    }

    template<class, class T, class TName>
    auto create_impl(const aux::type<type_traits::named<TName, T>>&) const {
        return create_t<T, TName>();
    }

    template<class T, class TAction>
    std::enable_if_t<has_call<T, const TAction&>{}>
    call_impl(const TAction& action) {
        static_cast<T&>(*this).call(action);
    }

    template<class T, class TAction>
    std::enable_if_t<!has_call<T, const TAction&>{}>
    call_impl(const TAction&) { }

    template<class TInjector, class... Ts>
    auto create_from_injector(const TInjector& injector
                            , const aux::type_list<Ts...>&) const noexcept {
        return pool_t{Ts{injector}...};
    }

    TConfig config_;
};

}}} // boost::di::core

#endif

