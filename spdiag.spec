%{!?spdiag_percentile:%global spdiag_percentile 0}
%{!?spdiag_perflog:%global spdiag_perflog 0}
%{!?spdiag_mempoint:%global spdiag_mempoint 0}
%{!?spdiag_ob_memory:%global spdiag_ob_memory 1}
%{!?spdiag_ob_cache:%global spdiag_ob_cache 1}

Name:           spdiag
Version:        1.0.0
Release:        1%{?spdiag_release_suffix}%{?dist}
Summary:        Zero-overhead performance diagnostics framework for embedded systems

License:        MulanPSL-2.0
URL:            https://gitcode.com/liusiyu60/spdiag
Source0:        %{name}-%{version}.tar.gz

ExclusiveArch:  aarch64

BuildRequires:  cmake >= 3.16
BuildRequires:  make
BuildRequires:  gcc-c++
BuildRequires:  clang
BuildRequires:  libbpf-devel
BuildRequires:  bpftool
BuildRequires:  elfutils-libelf-devel
BuildRequires:  zlib-devel
BuildRequires:  systemtap-sdt-devel
BuildRequires:  gtest-devel
BuildRequires:  kernel-headers
BuildRequires:  pkgconfig

Requires:       libbpf
Requires:       elfutils-libelf
Requires:       zlib

%description
SpDiag is a C++17 embedded performance diagnostics framework that achieves
zero-overhead hot-path profiling through compile-time X-Macro enumerations,
shared memory, and atomic operations. It provides the following major features:

  - PerfPoint: compile-time defined performance counters
  - Global PerfPoint: thread-safe process-wide performance aggregation
  - OB Memstat: non-intrusive memory allocation tracking via eBPF
  - OB Cachestat: cache hit rate monitoring via perf_event
  - MemPoint: point-level memory observation via USDT + eBPF

%package devel
Summary:        Development files for SpDiag
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
Header files and the CMake package configuration required to build
applications with the SpDiag SDK.

%package static
Summary:        Static libraries for SpDiag
Requires:       %{name}-devel%{?_isa} = %{version}-%{release}

%description static
Static libraries for applications that link the SpDiag SDK without a
runtime dependency on the SpDiag shared library.

%prep
%autosetup -p1

%build
%set_build_flags
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir} \
    -DCMAKE_SKIP_INSTALL_RPATH=ON \
    -DBUILD_TESTS=ON \
    -DBUILD_EXAMPLES=OFF \
    -DENABLE_PERCENTILE=%{spdiag_percentile} \
    -DENABLE_PERFLOG=%{spdiag_perflog} \
    -DENABLE_MEMPOINT=%{spdiag_mempoint} \
    -DENABLE_OB_MEMORY=%{spdiag_ob_memory} \
    -DENABLE_OB_CACHE=%{spdiag_ob_cache} \
    -DSPDIAG_BUILD_SHARED=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
%make_build -C build

cmake -S . -B build-static \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir} \
    -DCMAKE_SKIP_INSTALL_RPATH=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DENABLE_PERCENTILE=%{spdiag_percentile} \
    -DENABLE_PERFLOG=%{spdiag_perflog} \
    -DENABLE_MEMPOINT=%{spdiag_mempoint} \
    -DENABLE_OB_MEMORY=%{spdiag_ob_memory} \
    -DENABLE_OB_CACHE=%{spdiag_ob_cache} \
    -DSPDIAG_BUILD_SHARED=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
%make_build -C build-static spdiag_lib

%install
DESTDIR=%{buildroot} cmake --install build
DESTDIR=%{buildroot} cmake --install build-static \
    --component SpDiagStaticDevelopment

%check
ctest --test-dir build --output-on-failure %{?_smp_mflags}

%files
%defattr(-,root,root,-)
%license LICENSE
%doc README.md QuickStart.md
%{_bindir}/spdiag
%{_libdir}/libspdiag.so.*
%dir %{_sysconfdir}/spdiag
%config(noreplace) %{_sysconfdir}/spdiag/spdiag.conf

%files devel
%defattr(-,root,root,-)
%{_libdir}/libspdiag.so
%{_includedir}/spdiag/
%{_libdir}/cmake/SpDiag/

%files static
%defattr(-,root,root,-)
%{_libdir}/libspdiag.a
%{_libdir}/libspdiag_logger.a
%{_libdir}/cmake/SpDiag/SpDiagStaticTargets*.cmake

%changelog
* Wed Aug 05 2026 liusiyu60 <liusiyu605@huawei.com> - 1.0.0-1
- Type:Feature
- ID:NA
- SUG:NA
- DESC:SpDiag 1.0.0 first stable release.
