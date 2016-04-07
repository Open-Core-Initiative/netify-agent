# Netify DPI Daemon

Name: netifyd
Version: 1.1
Release: 4%{dist}
Vendor: eGloo Incorporated
License: GPL
Group: System/Daemons
Packager: eGloo Incorporated
Source: %{name}-%{version}.tar.gz
BuildRoot: /var/tmp/%{name}-%{version}
Obsoletes: cdpid
BuildRequires: autoconf >= 2.63
BuildRequires: automake
BuildRequires: json-c-devel
BuildRequires: libcurl-devel
BuildRequires: libpcap-devel
BuildRequires: libtool
BuildRequires: pkgconfig
BuildRequires: systemd
BuildRequires: zlib-devel
%if "0%{dist}" == "0.v7"
Requires: webconfig-httpd
Requires: app-network-core
%endif
%{?systemd_requires}
Summary: Netify DPI Daemon

%description
Netify DPI Daemon
Report bugs to: http://www.egloo.ca/gnats

# Build
%prep
%setup -q
./autogen.sh
ac_flags="--with-pic=inih --with-pic=ndpi"
%if "0%{dist}" == "0.v7"
ac_flags="$ac_flags --enable-netify-sink"
%endif
%{configure} $ac_flags
%build
make %{?_smp_mflags}

# Install
%install
make install DESTDIR=%{buildroot}
rm -rf %{buildroot}/%{_libdir}
rm -rf %{buildroot}/%{_includedir}
rm -rf %{buildroot}/%{_bindir}
mkdir -p %{buildroot}/%{_sharedstatedir}/%{name}
mkdir -p %{buildroot}/%{_sysconfdir}
install -D -m 644 deploy/%{name}.service %{buildroot}/%{_unitdir}/%{name}.service
%if "0%{dist}" == "0.v7"
install -D -m 0644 deploy/clearos/%{name}.tmpf %{buildroot}/%{_tmpfilesdir}/%{name}.conf
install -D -m 0660 deploy/clearos/%{name}.conf %{buildroot}/%{_sysconfdir}/%{name}.conf
install -D -m 0755 deploy/clearos/exec-pre.sh %{buildroot}/%{_libexecdir}/%{name}/exec-pre.sh
%else
install -D -m 0644 deploy/%{name}.tmpf %{buildroot}/%{_tmpfilesdir}/%{name}.conf
install -D -m 0660 deploy/%{name}.conf %{buildroot}/%{_sysconfdir}/%{name}.conf
install -D -m 0755 deploy/exec-pre.sh %{buildroot}/%{_libexecdir}/%{name}/exec-pre.sh
%endif
mkdir -p %{buildroot}/run
install -d -m 0755 %{buildroot}/run/%{name}

# Clean-up
%clean
[ "%{buildroot}" != "/" ] && rm -rf %{buildroot}

# Post install
%post
%systemd_post %{name}.service
if `egrep -q '^uuid[[:space:]]*=[[:space:]]*00-00-00-00$' %{_sysconfdir}/%{name}.conf 2>/dev/null`; then
    uuid=$(%{_sbindir}/%{name} -U 2>/dev/null)
    if [ -z "$uuid" ]; then
        echo "Error generating UUID."
    else
        sed -e "s/^uuid[[:space:]]*=[[:space:]]*00-00-00-00/uuid = $uuid/" -i %{_sysconfdir}/%{name}.conf
    fi
fi

# Pre uninstall
%preun
%systemd_preun %{name}.service

# Post uninstall
%postun
%systemd_postun_with_restart %{name}.service

# Files
%files
%defattr(-,root,root)
%attr(644,root,root) %{_tmpfilesdir}/%{name}.conf
%attr(644,root,root) %{_unitdir}/%{name}.service
%attr(750,root,webconfig) %{_sharedstatedir}/%{name}/
%attr(755,root,root) %{_libexecdir}/%{name}/
%config(noreplace) %attr(660,root,webconfig) %{_sysconfdir}/%{name}.conf
%dir /run/%{name}
%{_sbindir}/%{name}

# vi: expandtab shiftwidth=4 softtabstop=4 tabstop=4
