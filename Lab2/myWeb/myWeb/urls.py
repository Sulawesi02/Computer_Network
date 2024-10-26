from django.contrib import admin
from django.urls import path
from Lab2.views import index   # 引入视图函数

urlpatterns = [
    path('admin/', admin.site.urls),
    path('', index),  # 添加我们的主页路径
]