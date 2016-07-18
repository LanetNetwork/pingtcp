Name:           pingtcp
Version:        0.0.4
Release:        1%{?dist}
Summary:        Small utility to measure TCP handshake time (torify-friendly)

License:        GPLv3
URL:            https://github.com/LanetNetwork/pingtcp
Source0:        pingtcp-0.0.4.tar.gz

BuildRequires:   gcc cmake make gperftools-devel libunwind-devel
Requires:        gperftools-devel libunwind

%description
Small utility to measure TCP handshake time (torify-friendly)

%prep
%setup -q

%build
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=%{buildroot}%{_prefix} ..
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
cd build
make install

%clean
rm -rf %{buildroot}

%files
%defattr(0644, root, root, 0755)
%doc COPYING README.md
%attr(0755, root, root) %{_bindir}/%{name}

%changelog
