// -*- C++ -*-
//===-- parallel_backend_sycl_utils.h -------------------------------------===//
//
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file incorporates work covered by the following copyright and permission
// notice:
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef _ONEDPL_PARALLEL_BACKEND_SYCL_UTILS_H
#define _ONEDPL_PARALLEL_BACKEND_SYCL_UTILS_H

//!!! NOTE: This file should be included under the macro _ONEDPL_BACKEND_SYCL
#include <memory>
#include <type_traits>
#include <tuple>

#include "../../iterator_impl.h"

#include "sycl_defs.h"
#include "execution_sycl_defs.h"
#include "sycl_iterator.h"
#include "../../utils.h"

#if _ONEDPL_DEBUG_SYCL
#    include <iostream>
#endif

#define _PRINT_INFO_IN_DEBUG_MODE(...)                                                                                 \
    oneapi::dpl::__par_backend_hetero::__internal::__print_device_debug_info(__VA_ARGS__)

namespace oneapi
{
namespace dpl
{
namespace __internal
{

//-----------------------------------------------------------------------------
// Device run-time information helpers
//-----------------------------------------------------------------------------

#if _ONEDPL_DEBUG_SYCL
template <typename _ExecutionPolicy>
::std::string
__device_info(const _ExecutionPolicy& __policy)
{
    return __policy.queue().get_device().template get_info<sycl::info::device::name>();
}
#endif

template <typename _ExecutionPolicy>
::std::size_t
__max_work_group_size(const _ExecutionPolicy& __policy)
{
    return __policy.queue().get_device().template get_info<sycl::info::device::max_work_group_size>();
}

template <typename _ExecutionPolicy, typename _Size>
_Size
__slm_adjusted_work_group_size(const _ExecutionPolicy& __policy, _Size __local_mem_per_wi, _Size __wg_size = 0)
{
    if (__wg_size == 0)
        __wg_size = __max_work_group_size(__policy);
    auto __local_mem_size = __policy.queue().get_device().template get_info<sycl::info::device::local_mem_size>();
    return sycl::min(__local_mem_size / __local_mem_per_wi, __wg_size);
}

#if _USE_SUB_GROUPS
template <typename _ExecutionPolicy>
::std::size_t
__max_sub_group_size(const _ExecutionPolicy& __policy)
{
    auto __supported_sg_sizes = __policy.queue().get_device().template get_info<sycl::info::device::sub_group_sizes>();
    //The result of get_info<sycl::info::device::sub_group_sizes>() can be empty; if so, return 0
    return __supported_sg_sizes.empty() ? 0 : __supported_sg_sizes.back();
}
#endif

template <typename _ExecutionPolicy>
::std::uint32_t
__max_compute_units(const _ExecutionPolicy& __policy)
{
    return __policy.queue().get_device().template get_info<sycl::info::device::max_compute_units>();
}

//-----------------------------------------------------------------------------
// Kernel run-time information helpers
//-----------------------------------------------------------------------------

// 20201214 value corresponds to Intel(R) oneAPI C++ Compiler Classic 2021.1.2 Patch release
#define _USE_KERNEL_DEVICE_SPECIFIC_API (__SYCL_COMPILER_VERSION > 20201214) || (_ONEDPL_LIBSYCL_VERSION >= 50700)

template <typename _ExecutionPolicy>
::std::size_t
__kernel_work_group_size(const _ExecutionPolicy& __policy, const sycl::kernel& __kernel)
{
    const sycl::device& __device = __policy.queue().get_device();
    ::std::size_t __max_wg_size =
#if _USE_KERNEL_DEVICE_SPECIFIC_API
        __kernel.template get_info<sycl::info::kernel_device_specific::work_group_size>(__device);
#else
        __kernel.template get_work_group_info<sycl::info::kernel_work_group::work_group_size>(__device);
#endif
    // The variable below is needed to achieve better performance on CPU devices.
    // Experimentally it was found that the most common divisor is 4 with all patterns.
    // TODO: choose the divisor according to specific pattern.
    if (__device.is_cpu() && __max_wg_size >= 4)
        __max_wg_size /= 4;

    return __max_wg_size;
}

template <typename _ExecutionPolicy>
::std::uint32_t
__kernel_sub_group_size(const _ExecutionPolicy& __policy, const sycl::kernel& __kernel)
{
    const sycl::device& __device = __policy.queue().get_device();
    [[maybe_unused]] const ::std::size_t __wg_size = __kernel_work_group_size(__policy, __kernel);
    const ::std::uint32_t __sg_size =
#if _USE_KERNEL_DEVICE_SPECIFIC_API
        __kernel.template get_info<sycl::info::kernel_device_specific::max_sub_group_size>(
            __device
#    if _ONEDPL_LIBSYCL_VERSION < 60000
            ,
            sycl::range<3> { __wg_size, 1, 1 }
#    endif
        );
#else
        __kernel.template get_sub_group_info<sycl::info::kernel_sub_group::max_sub_group_size>(
            __device, sycl::range<3>{__wg_size, 1, 1});
#endif
    return __sg_size;
}
//-----------------------------------------------------------------------------

} // namespace __internal

namespace __par_backend_hetero
{

// aliases for faster access to modes
using access_mode = sycl::access_mode;

// function to simplify zip_iterator creation
template <typename... T>
oneapi::dpl::zip_iterator<T...>
zip(T... args)
{
    return oneapi::dpl::zip_iterator<T...>(args...);
}

// function is needed to wrap kernel name into another policy class
template <template <typename> class _NewKernelName, typename _Policy,
          oneapi::dpl::__internal::__enable_if_device_execution_policy<_Policy, int> = 0>
auto
make_wrapped_policy(_Policy&& __policy)
    -> decltype(oneapi::dpl::execution::make_device_policy<
                _NewKernelName<typename ::std::decay_t<_Policy>::kernel_name>>(::std::forward<_Policy>(__policy)))
{
    return oneapi::dpl::execution::make_device_policy<_NewKernelName<typename ::std::decay_t<_Policy>::kernel_name>>(
        ::std::forward<_Policy>(__policy));
}

#if _ONEDPL_FPGA_DEVICE
template <template <typename> class _NewKernelName, typename _Policy,
          oneapi::dpl::__internal::__enable_if_fpga_execution_policy<_Policy, int> = 0>
auto
make_wrapped_policy(_Policy&& __policy)
    -> decltype(oneapi::dpl::execution::make_fpga_policy<::std::decay_t<_Policy>::unroll_factor,
                                                         _NewKernelName<typename ::std::decay_t<_Policy>::kernel_name>>(
        ::std::forward<_Policy>(__policy)))
{
    return oneapi::dpl::execution::make_fpga_policy<::std::decay_t<_Policy>::unroll_factor,
                                                    _NewKernelName<typename ::std::decay_t<_Policy>::kernel_name>>(
        ::std::forward<_Policy>(__policy));
}
#endif

namespace __internal
{

//-----------------------------------------------------------------------
// Kernel name generation helpers
//-----------------------------------------------------------------------

// extract the deepest kernel name when we have a policy wrapper that might hide the default name
template <typename _CustomName>
struct _HasDefaultName
{
    static constexpr bool value = ::std::is_same_v<_CustomName, oneapi::dpl::execution::DefaultKernelName>
#if _ONEDPL_FPGA_DEVICE
                                  || ::std::is_same_v<_CustomName, oneapi::dpl::execution::DefaultKernelNameFPGA>
#endif
        ;
};

template <template <typename...> class _ExternalName, typename... _InternalName>
struct _HasDefaultName<_ExternalName<_InternalName...>>
{
    static constexpr bool value = (... || _HasDefaultName<_InternalName>::value);
};

template <typename... _Name>
struct __optional_kernel_name;

template <typename _CustomName>
using __kernel_name_provider =
#if __SYCL_UNNAMED_LAMBDA__
    ::std::conditional_t<_HasDefaultName<_CustomName>::value, __optional_kernel_name<>,
                         __optional_kernel_name<_CustomName>>;
#else
    __optional_kernel_name<_CustomName>;
#endif

template <typename _KernelName, char...>
struct __composite
{
};

// Compose kernel name by transforming the constexpr string to the sequence of chars
// and instantiate template with variadic non-type template parameters.
// This approach is required to get reliable work group size when kernel is unnamed
#if _ONEDPL_BUILT_IN_STABLE_NAME_PRESENT
template <typename _KernelName, typename _Tp>
class __kernel_name_composer
{
    static constexpr auto __name = __builtin_sycl_unique_stable_name(_Tp);
    static constexpr ::std::size_t __name_size = __builtin_strlen(__name);

    template <::std::size_t... _Is>
    static __composite<_KernelName, __name[_Is]...>
    __compose_kernel_name(::std::index_sequence<_Is...>);

  public:
    using type = decltype(__compose_kernel_name(::std::make_index_sequence<__name_size>{}));
};
#endif // _ONEDPL_BUILT_IN_STABLE_NAME_PRESENT

template <template <typename...> class _BaseName, typename _CustomName, typename... _Args>
using __kernel_name_generator =
#if __SYCL_UNNAMED_LAMBDA__
    ::std::conditional_t<_HasDefaultName<_CustomName>::value,
#    if _ONEDPL_BUILT_IN_STABLE_NAME_PRESENT
                         typename __kernel_name_composer<_BaseName<>, _BaseName<_CustomName, _Args...>>::type,
#    else // _ONEDPL_BUILT_IN_STABLE_NAME_PRESENT
                         _BaseName<_CustomName, _Args...>,
#    endif
                         _BaseName<_CustomName>>;
#else // __SYCL_UNNAMED_LAMBDA__
    _BaseName<_CustomName>;
#endif

template <typename... _KernelNames>
class __kernel_compiler
{
    static constexpr ::std::size_t __kernel_count = sizeof...(_KernelNames);
    using __kernel_array_type = ::std::array<sycl::kernel, __kernel_count>;

    static_assert(__kernel_count > 0, "At least one kernel name should be provided");

  public:
#if _ONEDPL_KERNEL_BUNDLE_PRESENT
    template <typename _Exec>
    static auto
    __compile(_Exec&& __exec)
    {
        ::std::vector<sycl::kernel_id> __kernel_ids{sycl::get_kernel_id<_KernelNames>()...};

        auto __kernel_bundle = sycl::get_kernel_bundle<sycl::bundle_state::executable>(
            __exec.queue().get_context(), {__exec.queue().get_device()}, __kernel_ids);

        if constexpr (__kernel_count > 1)
            return __make_kernels_array(__kernel_bundle, __kernel_ids, ::std::make_index_sequence<__kernel_count>());
        else
            return __kernel_bundle.template get_kernel(__kernel_ids[0]);
    }

  private:
    template <typename _KernelBundle, typename _KernelIds, ::std::size_t... _Ip>
    static auto
    __make_kernels_array(_KernelBundle __kernel_bundle, _KernelIds& __kernel_ids, ::std::index_sequence<_Ip...>)
    {
        return __kernel_array_type{__kernel_bundle.template get_kernel(__kernel_ids[_Ip])...};
    }
#else
    template <typename _Exec>
    static auto
    __compile(_Exec&& __exec)
    {
        sycl::program __program(__exec.queue().get_context());

        using __return_type = std::conditional_t<(__kernel_count > 1), __kernel_array_type, sycl::kernel>;
        return __return_type{
            (__program.build_with_kernel_type<_KernelNames>(), __program.get_kernel<_KernelNames>())...};
    }
#endif
};

#if _ONEDPL_DEBUG_SYCL
template <typename _Policy>
inline void
// Passing policy by value should be enough for debugging
__print_device_debug_info(_Policy __policy, size_t __wg_size = 0, size_t __max_cu = 0)
{
    ::std::cout << "Device info" << ::std::endl;
    ::std::cout << " > device name:         " << oneapi::dpl::__internal::__device_info(__policy) << ::std::endl;
    ::std::cout << " > max compute units:   "
                << (__max_cu ? __max_cu : oneapi::dpl::__internal::__max_compute_units(__policy)) << ::std::endl;
    ::std::cout << " > max work-group size: "
                << (__wg_size ? __wg_size : oneapi::dpl::__internal::__max_work_group_size(__policy)) << ::std::endl;
}
#else
template <typename _Policy>
inline void __print_device_debug_info(_Policy, size_t = 0, size_t = 0)
{
}
#endif

//-----------------------------------------------------------------------
// type traits for comparators
//-----------------------------------------------------------------------

// traits for ascending functors
template <typename _Comp>
struct __is_comp_ascending
{
    static constexpr bool value = false;
};
template <typename _T>
struct __is_comp_ascending<::std::less<_T>>
{
    static constexpr bool value = true;
};
template <>
struct __is_comp_ascending<oneapi::dpl::__internal::__pstl_less>
{
    static constexpr bool value = true;
};

// traits for descending functors
template <typename _Comp>
struct __is_comp_descending
{
    static constexpr bool value = false;
};
template <typename _T>
struct __is_comp_descending<::std::greater<_T>>
{
    static constexpr bool value = true;
};
template <>
struct __is_comp_descending<oneapi::dpl::__internal::__pstl_greater>
{
    static constexpr bool value = true;
};

//-----------------------------------------------------------------------
// temporary "buffer" constructed over specified container type
//-----------------------------------------------------------------------

template <typename _Unknown>
struct __local_buffer;

template <int __dim, typename _AllocT, typename _T>
struct __local_buffer<sycl::buffer<_T, __dim, _AllocT>>
{
    using type = sycl::buffer<_T, __dim, _AllocT>;
};

//if we take ::std::tuple as a type for buffer we should convert to internal::tuple
template <int __dim, typename _AllocT, typename... _T>
struct __local_buffer<sycl::buffer<::std::tuple<_T...>, __dim, _AllocT>>
{
    using type = sycl::buffer<oneapi::dpl::__internal::tuple<_T...>, __dim, _AllocT>;
};

// impl for sycl::buffer<...>
template <typename _ExecutionPolicy, typename _T>
class __buffer_impl
{
  private:
    using __container_t = typename __local_buffer<sycl::buffer<_T>>::type;

    __container_t __container;

  public:
    static_assert(::std::is_same_v<_ExecutionPolicy, ::std::decay_t<_ExecutionPolicy>>);

    __buffer_impl(_ExecutionPolicy /*__exec*/, ::std::size_t __n_elements) : __container{sycl::range<1>(__n_elements)}
    {
    }

    auto
    get() -> decltype(oneapi::dpl::begin(__container)) const
    {
        return oneapi::dpl::begin(__container);
    }

    __container_t
    get_buffer() const
    {
        return __container;
    }
};

template <typename _ExecutionPolicy, typename _T>
struct __sycl_usm_free
{
    _ExecutionPolicy __exec;

    void
    operator()(_T* __memory) const
    {
        sycl::free(__memory, __exec.queue().get_context());
    }
};

template <typename _ExecutionPolicy, typename _T, sycl::usm::alloc __alloc_t>
struct __sycl_usm_alloc
{
    _ExecutionPolicy __exec;

    _T*
    operator()(::std::size_t __elements) const
    {
        const auto& __queue = __exec.queue();
        return (_T*)sycl::malloc(sizeof(_T) * __elements, __queue.get_device(), __queue.get_context(), __alloc_t);
    }
};

//-----------------------------------------------------------------------
// type traits for objects granting access to some value objects
//-----------------------------------------------------------------------

template <typename _ContainerOrIterator>
struct __memobj_traits
{
    using value_type = typename _ContainerOrIterator::value_type;
};

template <typename _T>
struct __memobj_traits<_T*>
{
    using value_type = _T;
};

} // namespace __internal

template <typename _ExecutionPolicy, typename _T>
using __buffer = __internal::__buffer_impl<::std::decay_t<_ExecutionPolicy>, _T>;

template <typename T>
struct __repacked_tuple
{
    using type = T;
};

template <typename... Args>
struct __repacked_tuple<::std::tuple<Args...>>
{
    using type = oneapi::dpl::__internal::tuple<Args...>;
};

template <typename T>
using __repacked_tuple_t = typename __repacked_tuple<T>::type;

template <typename _ContainerOrIterable>
using __value_t = typename __internal::__memobj_traits<_ContainerOrIterable>::value_type;

template <typename _T>
struct __accessor
{
  private:
    using __accessor_t = sycl::accessor<_T, 1, sycl::access::mode::read_write, __dpl_sycl::__target_device,
                                        sycl::access::placeholder::false_t>;
    __accessor_t __acc;
    _T* __ptr = nullptr;
    bool __usm = false;

  public:
// A buffer is used by default. Supporting compilers use the unified future on top of USM host memory or a buffer.
#if _ONEDPL_SYCL_USM_HOST_PRESENT
    __accessor(sycl::handler& __cgh, bool __u, ::std::shared_ptr<sycl::buffer<_T, 1>> __sycl_buf,
               ::std::shared_ptr<_T> __usm_buf)
        : __usm(__u)
    {
        if (__usm)
            __ptr = __usm_buf.get();
        else
            __acc = sycl::accessor(*__sycl_buf, __cgh, sycl::read_write, __dpl_sycl::__no_init{});
    }
#else
    __accessor(sycl::handler& __cgh, bool, ::std::shared_ptr<sycl::buffer<_T, 1>> __sycl_buf,
               ::std::shared_ptr<_T> __usm_buf)
        : __usm(false), __acc(sycl::accessor(*__sycl_buf, __cgh, sycl::read_write, __dpl_sycl::__no_init{}))
    {
    }
#endif

    auto
    __get_pointer() const // should be cached within a kernel
    {
        return __usm ? __ptr : &__acc[0];
    }
};

template <typename _ExecutionPolicy, typename _T>
struct __storage
{
  private:
    using __sycl_buffer_t = sycl::buffer<_T, 1>;
    ::std::shared_ptr<__sycl_buffer_t> __sycl_buf;
    ::std::shared_ptr<_T> __usm_buf;
    bool __usm;

  public:
    __storage(_ExecutionPolicy& __exec, bool __u, ::std::size_t __n) : __usm(__u)
    {
        if (__usm)
        {
            __usm_buf = std::shared_ptr<_T>(
                __internal::__sycl_usm_alloc<_ExecutionPolicy, _T, sycl::usm::alloc::host>{__exec}(__n),
                __internal::__sycl_usm_free<_ExecutionPolicy, _T>{__exec});
        }
        else
            __sycl_buf = ::std::make_shared<__sycl_buffer_t>(__sycl_buffer_t(__n));
    }

    auto
    __get_acc(sycl::handler& __cgh)
    {
        return __accessor<_T>(__cgh, __usm, __sycl_buf, __usm_buf);
    }

    auto
    __get_value(size_t idx = 0)
    {
        return __usm ? *(__usm_buf.get() + idx) : __sycl_buf->get_host_access(sycl::read_only)[idx];
    }

    bool
    __get_usm() const
    {
        return __usm;
    }
};

//A contract for future class: <sycl::event or other event, a value, sycl::buffers..., or __storage (USM or buffer)>
//Impl details: inheritance (private) instead of aggregation for enabling the empty base optimization.
template <typename _Event, typename... _Args>
class __future : private std::tuple<_Args...>
{
    _Event __my_event;

    template <typename _T>
    constexpr auto
    __wait_and_get_value(sycl::buffer<_T>& __buf)
    {
        //according to a contract, returned value is one-element sycl::buffer
        return __buf.get_host_access(sycl::read_only)[0];
    }

    template <typename _ExecutionPolicy, typename _T>
    constexpr auto
    __wait_and_get_value(__storage<_ExecutionPolicy, _T>& __buf)
    {
        // Explicit wait in case of USM memory. Buffer accessors are synchronous.
        if (__buf.__get_usm())
            wait();
        return __buf.__get_value();
    }

    template <typename _T>
    constexpr auto
    __wait_and_get_value(_T& __val)
    {
        wait();
        return __val;
    }

  public:
    __future(_Event __e, _Args... __args) : std::tuple<_Args...>(__args...), __my_event(__e) {}
    __future(_Event __e, std::tuple<_Args...> __t) : std::tuple<_Args...>(__t), __my_event(__e) {}

    auto
    event() const
    {
        return __my_event;
    }
    operator _Event() const { return event(); }
    void
    wait()
    {
#if !ONEDPL_ALLOW_DEFERRED_WAITING
        __my_event.wait_and_throw();
#endif
    }

    auto
    get()
    {
        if constexpr (sizeof...(_Args) > 0)
        {
            auto& __val = std::get<0>(*this);
            return __wait_and_get_value(__val);
        }
        else
            wait();
    }

    //The internal API. There are cases where the implementation specifies return value  "higher" than SYCL backend,
    //where a future is created.
    template <typename _T>
    auto
    __make_future(_T __t) const
    {
        auto new_val = std::tuple<_T>(__t);
        auto new_tuple = std::tuple_cat(new_val, (std::tuple<_Args...>)*this);
        return __future<_Event, _T, _Args...>(__my_event, new_tuple);
    }
};

// Only use USM host allocations on L0 GPUs. Other devices show significant slowdowns and will use a buffer instead.
inline bool
__use_USM_host_allocations(sycl::queue __queue)
{
// A buffer is used by default. Supporting compilers use the unified future on top of USM host memory or a buffer.
#if _ONEDPL_SYCL_USM_HOST_PRESENT
    auto __device = __queue.get_device();
    if (!__device.is_gpu())
        return false;
    if (!__device.has(sycl::aspect::usm_host_allocations))
        return false;
    if (__device.get_backend() != sycl::backend::ext_oneapi_level_zero)
        return false;
    return true;
#else
    return false;
#endif
}

// Invoke a callable and pass a compile-time integer based on a provided run-time integer.
// The compile-time integer that will be provided to the callable is defined as the smallest
// value in the integer_sequence not less than the run-time integer. For example:
//
//   __static_monotonic_dispatcher<::std::integer_sequence<::std::uint16_t, 2, 4, 8, 16>::__dispatch(f, 3);
//
// will call f<4>(), since 4 is the smallest value in the sequence not less than 3.
//
// If there are no values in the sequence less than the run-time integer, the last value in
// the sequence will be used.
//
// Note that the integers provided in the integer_sequence must be monotonically increasing
template <typename>
class __static_monotonic_dispatcher;

template <::std::uint16_t _X, ::std::uint16_t... _Xs>
class __static_monotonic_dispatcher<::std::integer_sequence<::std::uint16_t, _X, _Xs...>>
{
    template <::std::uint16_t... _Vals>
    using _Head = typename ::std::conditional_t<
        sizeof...(_Vals) != 0,
        ::std::tuple_element<0, ::std::tuple<::std::integral_constant<::std::uint32_t, _Vals>...>>,
        ::std::integral_constant<::std::uint32_t, ::std::numeric_limits<::std::uint32_t>::max()>>::type;

    static_assert(_X < _Head<_Xs...>::value, "Sequence must be monotonically increasing");

  public:
    template <typename _F, typename... _Args>
    static auto
    __dispatch(_F&& __f, ::std::uint16_t __x, _Args&&... args)
    {
        if constexpr (sizeof...(_Xs) == 0)
        {
            return ::std::forward<_F>(__f).template operator()<_X>(::std::forward<_Args>(args)...);
        }
        else
        {
            if (__x <= _X)
                return ::std::forward<_F>(__f).template operator()<_X>(::std::forward<_Args>(args)...);
            else
                return __static_monotonic_dispatcher<::std::integer_sequence<::std::uint16_t, _Xs...>>::__dispatch(
                    ::std::forward<_F>(__f), __x, ::std::forward<_Args>(args)...);
        }
    }
};

} // namespace __par_backend_hetero
} // namespace dpl
} // namespace oneapi

#endif //_ONEDPL_PARALLEL_BACKEND_SYCL_UTILS_H
