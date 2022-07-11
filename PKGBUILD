# Maintainer: Viacheslav Chepelyk-Kozhin <vaceslavkozin619@gmail.com>
pkgname=spmn
pkgver=1.0.3
pkgrel=1
pkgdesc="Suckless Package Manager"
arch=('x86_64')
url="https://github.com/slamko/spmn"
license=('GPL')
depends=('glibc' 'libbsd' 'xdg-utils' 'git')
makedepends=('git')
source=('https://github.com/slamko/spmn.git')
noextract=('packaging/')
md5sums=('SKIP')

prepare() {
	cd "$pkgname-$pkgver"
	git clone "https://github.com/slamko/zic.git"
}

build() {
	cd "$pkgname"
	make
}

package() {
	cd "$pkgname-$pkgver"
	make DESTDIR="$pkgdir/" install
}