Summary: allows daemons to be restarted without interruption of network availability
Name: SocketProtector
Version: %{ver}
Release: %{rel}%{?dist}
Group: System/Daemons
License: MIT
URL: https://github.com/kdt3rd/SocketProtector
Source0: %{name}-%{version}-%{release}.tar.gz

%description
SocketProtector is a small daemon that takes care of another command line program that is meant to serve a connection on an IP socket. It properly daemonizes, starts listening on the specified port, then spawns the specified program. It is expected that the program in question uses libSocketProtector to connect to the serving program. When SocketProtector is sent a SIGHUP, it will signal the child program to quit by closing the internal socket used for forwarding connections and respawn a new child program, giving time for the old instance to finish processing requests.

%prep
%setup -n %{name}-%{version}-%{release}

%build
echo "Building in %{buildroot}"
ninja

%define _incdir %{_prefix}/include
%install
mkdir -p %{buildroot}%{_bindir} %{buildroot}%{_libdir} %{buildroot}%{_incdir}
cp -p Build/SocketProtector %{buildroot}%{_bindir}/
strip -s %{buildroot}%{_bindir}/SocketProtector
cp -p Build/libSocketProtector.a %{buildroot}%{_libdir}/
cp -p lib/SocketProtector.h %{buildroot}%{_incdir}/

%files
%defattr(-, root, root)
%{_bindir}/*
%{_libdir}/*
%{_incdir}/*

%clean
rm -rf %{buildroot}

#The changelog is built automatically from Git history
%changelog
